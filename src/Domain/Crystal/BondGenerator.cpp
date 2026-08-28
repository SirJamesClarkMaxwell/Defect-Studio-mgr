#include "Core/dspch.hpp"

#include "Domain/Crystal/BondGenerator.hpp"

#include "Domain/Crystal/PeriodicGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>

namespace DefectStudio
{
	namespace
	{
		constexpr float kCellSize = 4.72f;
		constexpr float kMinimumBondDistanceSquared = 0.00001f;

		struct BondGridCellKey
		{
			int x = 0;
			int y = 0;
			int z = 0;

			[[nodiscard]] bool operator==(const BondGridCellKey &other) const noexcept
			{
				return x == other.x && y == other.y && z == other.z;
			}
		};

		struct BondGridCellKeyHasher
		{
			[[nodiscard]] std::size_t operator()(const BondGridCellKey &key) const noexcept
			{
				std::size_t seed = std::hash<int>{}(key.x);
				seed ^= std::hash<int>{}(key.y) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
				seed ^= std::hash<int>{}(key.z) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
				return seed;
			}
		};

		struct PotentialBondPair
		{
			std::size_t first = 0;
			std::size_t second = 0;
		};

		struct PeriodicPotentialBondPair
		{
			std::size_t first = 0;
			std::size_t second = 0;
			glm::ivec3 shift = glm::ivec3(0);
		};

		[[nodiscard]] std::array<BondGridCellKey, 27> BuildNeighborCellOffsets()
		{
			std::array<BondGridCellKey, 27> offsets{};
			std::size_t index = 0;
			for (int dx = -1; dx <= 1; ++dx)
			{
				for (int dy = -1; dy <= 1; ++dy)
				{
					for (int dz = -1; dz <= 1; ++dz)
					{
						offsets[index] = BondGridCellKey{dx, dy, dz};
						++index;
					}
				}
			}
			return offsets;
		}

		[[nodiscard]] BondGridCellKey CellForPosition(const glm::vec3 &position)
		{
			return BondGridCellKey{
				static_cast<int>(std::floor(position.x / kCellSize)),
				static_cast<int>(std::floor(position.y / kCellSize)),
				static_cast<int>(std::floor(position.z / kCellSize))};
		}

		// Bonds are undirected - (first, second, shift) and (second, first, -shift) describe the
		// same physical bond (the shift is always "how to move secondAtomIndex", so swapping which
		// atom is "second" flips its sign).
		[[nodiscard]] bool BondsMatch(
			const Bond &bond,
			std::size_t first,
			std::size_t second,
			const glm::ivec3 &shift)
		{
			if (bond.firstAtomIndex == first && bond.secondAtomIndex == second)
				return bond.periodicShift == shift;
			if (bond.firstAtomIndex == second && bond.secondAtomIndex == first)
				return bond.periodicShift == -shift;
			return false;
		}

		[[nodiscard]] bool HasBondBetween(
			const std::vector<Bond> &bonds,
			std::size_t first,
			std::size_t second,
			const glm::ivec3 &shift)
		{
			return std::any_of(bonds.begin(), bonds.end(), [&](const Bond &bond) {
				return BondsMatch(bond, first, second, shift);
			});
		}

		[[nodiscard]] bool IsLexicographicallyPositive(const glm::ivec3 &shift)
		{
			if (shift.x != 0)
				return shift.x > 0;
			if (shift.y != 0)
				return shift.y > 0;
			return shift.z > 0;
		}

		[[nodiscard]] float CutoffScaleForPair(
			const BondGenerationSettings &settings,
			const std::string &first,
			const std::string &second)
		{
			const auto found = settings.perPairCutoffOverride.find(BondPairKey(first, second));
			if (found != settings.perPairCutoffOverride.end())
				return found->second;
			return settings.globalCutoffScale;
		}

		// Largest cutoff any pair of species actually present in `atoms` could produce - used to
		// size the periodic-bonding safety margin below. Was previously a flat "2x kCellSize"
		// (9.44 A), which is calibrated for the *grid bucket* (deliberately oversized so a single
		// 27-cell neighbor search always covers any real cutoff) and ended up ~5x more
		// conservative than light elements like B/N/C actually need (~2 A cutoffs) - real defect
		// supercells with in-plane dimensions in the 5-9 A range were silently skipping periodic
		// bonding entirely under that threshold, even though nothing about the cell was degenerate.
		[[nodiscard]] float ComputeMaxPossibleCutoff(
			const std::vector<AtomSite> &atoms,
			const BondGenerationSettings &settings,
			const ElementPropertiesTable &elementPropertiesTable)
		{
			std::unordered_map<std::string, float> radiusBySpecies;
			for (const AtomSite &atom : atoms)
				radiusBySpecies.try_emplace(atom.species, elementPropertiesTable.Get(atom.species).covalentRadius);

			float maxCutoff = 0.0f;
			for (const auto &[speciesA, radiusA] : radiusBySpecies)
				for (const auto &[speciesB, radiusB] : radiusBySpecies)
					maxCutoff = std::max(maxCutoff, CutoffScaleForPair(settings, speciesA, speciesB) * (radiusA + radiusB));
			return maxCutoff;
		}

		// A structure's periodic images can only be found unambiguously (no overlap between a
		// bond and its own periodic copy) if the cell is comfortably larger than any bond cutoff -
		// skip periodic-image bonding entirely for a degenerate/placeholder cell (e.g. isolated-
		// molecule data with no meaningful lattice) rather than risk spurious bonds.
		[[nodiscard]] bool LatticeCellIsLargeEnoughForPeriodicBonding(const glm::mat3 &latticeMatrix, float maxCutoff)
		{
			const float minimumVectorLength = 2.0f * maxCutoff;
			for (int axis = 0; axis < 3; ++axis)
				if (glm::length(latticeMatrix[axis]) < minimumVectorLength)
					return false;
			return true;
		}

		using BondGridBuckets = std::unordered_map<BondGridCellKey, std::vector<std::size_t>, BondGridCellKeyHasher>;

		[[nodiscard]] BondGridBuckets BuildPositionBuckets(const std::vector<AtomSite> &atoms)
		{
			BondGridBuckets buckets;
			buckets.reserve(atoms.size() * 2u);
			for (std::size_t index = 0; index < atoms.size(); ++index)
				buckets[CellForPosition(atoms[index].position)].push_back(index);
			return buckets;
		}

		[[nodiscard]] std::vector<PotentialBondPair> BuildPotentialBondPairs(
			const std::vector<AtomSite> &atoms,
			const BondGridBuckets &buckets)
		{
			std::vector<PotentialBondPair> pairs;
			pairs.reserve(atoms.size() * 16u);
			const std::array<BondGridCellKey, 27> neighborOffsets = BuildNeighborCellOffsets();
			for (std::size_t first = 0; first < atoms.size(); ++first)
			{
				const BondGridCellKey baseCell = CellForPosition(atoms[first].position);
				for (const BondGridCellKey &offset : neighborOffsets)
				{
					const BondGridCellKey neighbor{
						baseCell.x + offset.x,
						baseCell.y + offset.y,
						baseCell.z + offset.z};
					const auto found = buckets.find(neighbor);
					if (found == buckets.end())
						continue;

					for (std::size_t second : found->second)
					{
						if (second > first)
							pairs.push_back(PotentialBondPair{first, second});
					}
				}
			}
			return pairs;
		}

		// Finds bonds that cross a periodic cell boundary: for every non-zero combination of
		// lattice-vector shifts, checks which real atoms sit near *another* atom's periodic image
		// (rather than reusing the plain Cartesian grid, which has no notion of wraparound and so
		// never finds these pairs on its own). `first < second` (or, for a rare self-image bond,
		// a lexicographically-positive shift) keeps each physical bond from being found twice, once
		// from each atom's perspective.
		[[nodiscard]] std::vector<PeriodicPotentialBondPair> BuildPeriodicPotentialBondPairs(
			const std::vector<AtomSite> &atoms,
			const glm::mat3 &latticeMatrix,
			const BondGridBuckets &buckets)
		{
			std::vector<PeriodicPotentialBondPair> pairs;
			const std::array<BondGridCellKey, 27> neighborOffsets = BuildNeighborCellOffsets();
			const std::array<glm::ivec3, 26> shifts = BuildNonZeroLatticeShifts();

			for (std::size_t second = 0; second < atoms.size(); ++second)
			{
				for (const glm::ivec3 &shift : shifts)
				{
					const glm::vec3 ghostPosition = atoms[second].position + CartesianShift(latticeMatrix, shift);
					const BondGridCellKey baseCell = CellForPosition(ghostPosition);
					for (const BondGridCellKey &offset : neighborOffsets)
					{
						const BondGridCellKey neighbor{
							baseCell.x + offset.x,
							baseCell.y + offset.y,
							baseCell.z + offset.z};
						const auto found = buckets.find(neighbor);
						if (found == buckets.end())
							continue;

						for (std::size_t first : found->second)
						{
							if (first > second)
								continue;
							if (first == second && !IsLexicographicallyPositive(shift))
								continue;
							pairs.push_back(PeriodicPotentialBondPair{first, second, shift});
						}
					}
				}
			}
			return pairs;
		}
	} // namespace

	std::string BondPairKey(std::string first, std::string second)
	{
		if (second < first)
			std::swap(first, second);
		return first + "-" + second;
	}

	void RegenerateAutoBonds(
		CrystalStructure &structure,
		const ElementPropertiesTable &elementPropertiesTable)
	{
		// A user-hidden Auto bond (Bond::visible = false, set by the bond-delete command) would
		// otherwise "come back" visible the next time any atom edit triggers a regen, since this
		// function used to erase every Auto bond and rebuild it from scratch with the visible=true
		// default. Snapshot which ones were hidden first (identity = endpoints + periodic shift,
		// same as BondsMatch/HasBondBetween use elsewhere in this file) and reapply after rebuild.
		std::vector<Bond> hiddenAutoBonds;
		for (const Bond &bond : structure.bonds)
			if (bond.origin == BondOrigin::Auto && !bond.visible)
				hiddenAutoBonds.push_back(bond);

		structure.bonds.erase(
			std::remove_if(structure.bonds.begin(), structure.bonds.end(), [](const Bond &bond) {
				return bond.origin == BondOrigin::Auto;
			}),
			structure.bonds.end());

		const auto tryAddBond = [&](std::size_t first, std::size_t second, const glm::ivec3 &shift, const glm::vec3 &secondPosition)
		{
			if (HasBondBetween(structure.bonds, first, second, shift))
				return;

			const AtomSite &atomA = structure.atoms[first];
			const AtomSite &atomB = structure.atoms[second];
			const float cutoffScale = CutoffScaleForPair(
				structure.bondSettings,
				atomA.species,
				atomB.species);
			const float cutoff = cutoffScale * (
				elementPropertiesTable.Get(atomA.species).covalentRadius +
				elementPropertiesTable.Get(atomB.species).covalentRadius);
			const float cutoffSquared = cutoff * cutoff;
			const glm::vec3 delta = atomA.position - secondPosition;
			const float distanceSquared = glm::dot(delta, delta);
			if (distanceSquared > cutoffSquared || distanceSquared < kMinimumBondDistanceSquared)
				return;

			Bond bond;
			bond.firstAtomIndex = first;
			bond.secondAtomIndex = second;
			bond.lengthAngstrom = std::sqrt(distanceSquared);
			bond.origin = BondOrigin::Auto;
			bond.periodicShift = shift;
			bond.visible = !std::any_of(hiddenAutoBonds.begin(), hiddenAutoBonds.end(), [&](const Bond &hidden) {
				return BondsMatch(hidden, first, second, shift);
			});
			structure.bonds.push_back(bond);
		};

		const BondGridBuckets buckets = BuildPositionBuckets(structure.atoms);
		for (const PotentialBondPair &pair : BuildPotentialBondPairs(structure.atoms, buckets))
			tryAddBond(pair.first, pair.second, glm::ivec3(0), structure.atoms[pair.second].position);

		const glm::mat3 latticeMatrix = structure.cell.ToMatrix();
		const float maxCutoff = ComputeMaxPossibleCutoff(structure.atoms, structure.bondSettings, elementPropertiesTable);
		if (structure.isPeriodic && LatticeCellIsLargeEnoughForPeriodicBonding(latticeMatrix, maxCutoff))
		{
			for (const PeriodicPotentialBondPair &pair : BuildPeriodicPotentialBondPairs(structure.atoms, latticeMatrix, buckets))
			{
				const glm::vec3 shiftedSecondPosition =
					structure.atoms[pair.second].position + CartesianShift(latticeMatrix, pair.shift);
				tryAddBond(pair.first, pair.second, pair.shift, shiftedSecondPosition);
			}
		}
	}
} // namespace DefectStudio
