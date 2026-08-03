#include "Core/dspch.hpp"

#include "Domain/Crystal/BondGenerator.hpp"

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

		[[nodiscard]] std::pair<std::size_t, std::size_t> NormalizeBondPair(std::size_t first, std::size_t second)
		{
			return first < second
				? std::pair<std::size_t, std::size_t>{first, second}
				: std::pair<std::size_t, std::size_t>{second, first};
		}

		[[nodiscard]] std::string PairKey(std::string first, std::string second)
		{
			if (second < first)
				std::swap(first, second);
			return first + "-" + second;
		}

		[[nodiscard]] bool HasBondBetween(
			const std::vector<Bond> &bonds,
			std::size_t first,
			std::size_t second)
		{
			const auto expected = NormalizeBondPair(first, second);
			return std::any_of(bonds.begin(), bonds.end(), [expected](const Bond &bond) {
				return NormalizeBondPair(bond.firstAtomIndex, bond.secondAtomIndex) == expected;
			});
		}

		[[nodiscard]] float CutoffScaleForPair(
			const BondGenerationSettings &settings,
			const std::string &first,
			const std::string &second)
		{
			const auto found = settings.perPairCutoffOverride.find(PairKey(first, second));
			if (found != settings.perPairCutoffOverride.end())
				return found->second;
			return settings.globalCutoffScale;
		}

		[[nodiscard]] std::vector<PotentialBondPair> BuildPotentialBondPairs(const std::vector<AtomSite> &atoms)
		{
			std::unordered_map<BondGridCellKey, std::vector<std::size_t>, BondGridCellKeyHasher> buckets;
			buckets.reserve(atoms.size() * 2u);

			for (std::size_t index = 0; index < atoms.size(); ++index)
				buckets[CellForPosition(atoms[index].position)].push_back(index);

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
	} // namespace

	void RegenerateAutoBonds(
		CrystalStructure &structure,
		const ElementPropertiesTable &elementPropertiesTable)
	{
		structure.bonds.erase(
			std::remove_if(structure.bonds.begin(), structure.bonds.end(), [](const Bond &bond) {
				return bond.origin == BondOrigin::Auto;
			}),
			structure.bonds.end());

		const std::vector<PotentialBondPair> potentialPairs = BuildPotentialBondPairs(structure.atoms);
		for (const PotentialBondPair &pair : potentialPairs)
		{
			if (HasBondBetween(structure.bonds, pair.first, pair.second))
				continue;

			const AtomSite &atomA = structure.atoms[pair.first];
			const AtomSite &atomB = structure.atoms[pair.second];
			const float cutoffScale = CutoffScaleForPair(
				structure.bondSettings,
				atomA.species,
				atomB.species);
			const float cutoff = cutoffScale * (
				elementPropertiesTable.Get(atomA.species).covalentRadius +
				elementPropertiesTable.Get(atomB.species).covalentRadius);
			const float cutoffSquared = cutoff * cutoff;
			const glm::vec3 delta = atomA.position - atomB.position;
			const float distanceSquared = glm::dot(delta, delta);
			if (distanceSquared > cutoffSquared || distanceSquared < kMinimumBondDistanceSquared)
				continue;

			Bond bond;
			bond.firstAtomIndex = pair.first;
			bond.secondAtomIndex = pair.second;
			bond.lengthAngstrom = std::sqrt(distanceSquared);
			bond.origin = BondOrigin::Auto;
			structure.bonds.push_back(bond);
		}
	}
} // namespace DefectStudio
