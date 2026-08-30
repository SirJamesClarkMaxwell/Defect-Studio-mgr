#include "Core/dspch.hpp"

#include "Domain/Crystal/StructureComparison.hpp"

#include "Domain/Crystal/PeriodicGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <glm/geometric.hpp>

// 2026-08-28: local spatial matching. The old implementation built one dense comparisonCount x
// referenceCount cost matrix and ran a single global Hungarian assignment over it. That let a
// vacancy or substitution's optimal-cost search "trade" displacement across the ENTIRE structure -
// on a regular lattice (e.g. hBN honeycomb), many atoms each shifted by one bond length to their
// neighbor's site is often cheaper in TOTAL cost than even one atom paying the sentinel
// (kUnmatchedDisplacementCost) for being reported as vacancy+interstitial, so Hungarian happily
// built long neighbor-hopping permutation chains across many lattice sites (reported 2026-08-28:
// arrows zigzagging through several hexagon rings). This file now finds only LOCAL candidate pairs
// (spatial bins, periodic-aware) first, partitions the resulting bipartite graph into connected
// components (union-find), and solves each component's local assignment independently -
// physically implausible cross-structure permutations can no longer form because atoms that are
// far apart never share a candidate edge at all, so they can never end up in the same component.

namespace DefectStudio
{
	namespace
	{
		// Same order of magnitude as a converged-relaxation displacement, but comfortably above
		// float noise - two structures meant to describe "the same cell" (POSCAR/CONTCAR pair, or
		// two defect variants built from the same supercell) shouldn't disagree on lattice vectors
		// by more than this.
		constexpr float kLatticeMatchToleranceAngstrom = 0.05f;

		[[nodiscard]] bool LatticesMatch(const glm::mat3 &a, const glm::mat3 &b)
		{
			for (int column = 0; column < 3; ++column)
				if (glm::length(a[column] - b[column]) > kLatticeMatchToleranceAngstrom)
					return false;
			return true;
		}

		[[nodiscard]] std::string MakeLatticeMismatchWarning()
		{
			return "Reference and comparison structures have different lattice vectors (beyond " +
				std::to_string(kLatticeMatchToleranceAngstrom) +
				" Angstrom tolerance) - periodic wrapping disabled, raw (non-periodic) distances used, "
				"so matches near a cell boundary may be missing or reported as vacancy/interstitial.";
		}

		// See LocalMatchingPlan::usePeriodicMatching for when this is/isn't used - only ever called
		// with usePeriodicMatching decided once per comparison, never per-pair.
		[[nodiscard]] glm::vec3 DisplacementDelta(
			bool usePeriodicMatching, const glm::mat3 &latticeMatrix, const glm::vec3 &a, const glm::vec3 &b)
		{
			if (usePeriodicMatching)
				return MinimumImageCartesianDelta(latticeMatrix, a, b);
			return a - b;
		}

		[[nodiscard]] float EffectiveMatchCutoff(
			const ElementPropertiesTable &elementPropertiesTable,
			const AtomSite &comparisonAtom,
			const AtomSite &referenceAtom,
			float maxMatchDisplacementAngstrom,
			float cutoffScale)
		{
			// maxMatchDisplacementAngstrom is the identity bound ("can this possibly be the same
			// atom after relaxation") and is species-independent by design. The covalent-radius
			// term is kept only as an ADDITIONAL, sometimes-tighter bound for light-element pairs -
			// for heavy elements its radius sum alone could exceed a full lattice spacing, which is
			// exactly what let far/wrong atoms match before (see BuildLocalMatchingPlan doc comment).
			const float comparisonRadius = elementPropertiesTable.Get(comparisonAtom.species).covalentRadius;
			const float referenceRadius = elementPropertiesTable.Get(referenceAtom.species).covalentRadius;
			return std::min(maxMatchDisplacementAngstrom, cutoffScale * (comparisonRadius + referenceRadius));
		}

		// One candidate (comparison atom, reference atom) pair close enough to plausibly be the
		// same physical atom - see BuildPeriodicCandidateEdges / BuildNonPeriodicCandidateEdges.
		struct CandidateEdge
		{
			std::size_t comparisonIndex;
			std::size_t referenceIndex;
			float distance;
		};

		// Periodic spatial bin search: bins the reference atoms into a coarse fractional-coordinate
		// grid (own cell only, wrapped modulo the grid so a bin near fractional 0 and one near
		// fractional 1 are neighbors), then for each comparison atom searches only the bins that
		// could possibly hold a reference atom within maxMatchDisplacementAngstrom (periodic
		// minimum-image distance), instead of comparing against every reference atom in the
		// structure. Neighbor bin range is bounded via FractionalSearchRadius (inverse-lattice row
		// norms), which stays correct for skewed/non-orthogonal cells, not just near-cubic ones.
		[[nodiscard]] std::vector<CandidateEdge> BuildPeriodicCandidateEdges(
			const CrystalStructure &reference,
			const CrystalStructure &comparison,
			const ElementPropertiesTable &elementPropertiesTable,
			float maxMatchDisplacementAngstrom,
			float cutoffScale,
			bool restrictToSameSpecies)
		{
			std::vector<CandidateEdge> edges;
			const std::size_t referenceCount = reference.atoms.size();
			const std::size_t comparisonCount = comparison.atoms.size();
			if (referenceCount == 0 || comparisonCount == 0)
				return edges;

			const glm::mat3 latticeMatrix = reference.cell.ToMatrix();
			const glm::mat3 inverseLatticeMatrix = reference.cell.ToInverseMatrix();

			// Coarse efficiency knob only (roughly one bin per maxMatchDisplacementAngstrom along
			// each lattice vector) - correctness comes from the neighbor-bin search range below, not
			// from this sizing, so it never needs to be exact.
			glm::ivec3 binCounts(1);
			for (int axis = 0; axis < 3; ++axis)
			{
				const float vectorLength = glm::length(latticeMatrix[axis]);
				binCounts[axis] = vectorLength > 1e-6f
					? std::max(1, static_cast<int>(std::floor(vectorLength / maxMatchDisplacementAngstrom)))
					: 1;
			}

			const auto wrapBinIndex = [](int index, int count) {
				const int wrapped = index % count;
				return wrapped < 0 ? wrapped + count : wrapped;
			};
			const auto fractionalToBin = [&](const glm::vec3 &fractional) {
				glm::ivec3 bin(0);
				for (int axis = 0; axis < 3; ++axis)
				{
					const float wrapped01 = fractional[axis] - std::floor(fractional[axis]);
					const int index = static_cast<int>(std::floor(wrapped01 * static_cast<float>(binCounts[axis])));
					bin[axis] = wrapBinIndex(index, binCounts[axis]);
				}
				return bin;
			};
			const auto flattenBin = [&](const glm::ivec3 &bin) {
				return (static_cast<std::size_t>(bin.x) * static_cast<std::size_t>(binCounts.y) +
						   static_cast<std::size_t>(bin.y)) *
						static_cast<std::size_t>(binCounts.z) +
					static_cast<std::size_t>(bin.z);
			};

			// Sparse map, NOT a dense binCounts.x*y*z array: a large cell with a small
			// maxMatchDisplacementAngstrom (e.g. a 2000A cell, ~2A cutoff -> 1000^3 bins) would
			// otherwise allocate a billion empty std::vector headers (~24GB) regardless of how few
			// atoms actually exist - confirmed as the direct cause of an OOM crash via test H's
			// MakeLargeCubicCell(2000.0f) fixture (2026-08-29).
			std::unordered_map<std::size_t, std::vector<std::size_t>> referenceBins;
			for (std::size_t referenceIndex = 0; referenceIndex < referenceCount; ++referenceIndex)
			{
				const glm::vec3 fractional = reference.CartesianToFractional(reference.atoms[referenceIndex].position);
				referenceBins[flattenBin(fractionalToBin(fractional))].push_back(referenceIndex);
			}

			const glm::vec3 fractionalSearchRadius =
				FractionalSearchRadius(inverseLatticeMatrix, maxMatchDisplacementAngstrom);
			glm::ivec3 binSearchRange(1);
			for (int axis = 0; axis < 3; ++axis)
				binSearchRange[axis] = std::max(
					1, static_cast<int>(std::ceil(fractionalSearchRadius[axis] * static_cast<float>(binCounts[axis]))));

			for (std::size_t comparisonIndex = 0; comparisonIndex < comparisonCount; ++comparisonIndex)
			{
				const AtomSite &comparisonAtom = comparison.atoms[comparisonIndex];
				const glm::vec3 comparisonFractional = reference.CartesianToFractional(comparisonAtom.position);
				const glm::ivec3 centerBin = fractionalToBin(comparisonFractional);

				// A small bin grid (binCounts axis <= 2*binSearchRange+1) revisits the same wrapped
				// bin more than once - dedupe per comparison atom so candidate/edge stats and the
				// resulting graph stay exact, not just "at least this many" duplicated edges.
				std::unordered_set<std::size_t> visitedBins;
				for (int dx = -binSearchRange.x; dx <= binSearchRange.x; ++dx)
					for (int dy = -binSearchRange.y; dy <= binSearchRange.y; ++dy)
						for (int dz = -binSearchRange.z; dz <= binSearchRange.z; ++dz)
						{
							const glm::ivec3 bin(
								wrapBinIndex(centerBin.x + dx, binCounts.x),
								wrapBinIndex(centerBin.y + dy, binCounts.y),
								wrapBinIndex(centerBin.z + dz, binCounts.z));
							const std::size_t flatBin = flattenBin(bin);
							if (!visitedBins.insert(flatBin).second)
								continue;

							const auto foundBin = referenceBins.find(flatBin);
							if (foundBin == referenceBins.end())
								continue;
							for (std::size_t referenceIndex : foundBin->second)
							{
								const AtomSite &referenceAtom = reference.atoms[referenceIndex];
								if (restrictToSameSpecies && referenceAtom.species != comparisonAtom.species)
									continue;

								const float effectiveCutoff = EffectiveMatchCutoff(
									elementPropertiesTable, comparisonAtom, referenceAtom, maxMatchDisplacementAngstrom,
									cutoffScale);
								const glm::vec3 delta = MinimumImageCartesianDelta(
									latticeMatrix, referenceAtom.position, comparisonAtom.position);
								const float distance = glm::length(delta);
								if (distance <= effectiveCutoff)
									edges.push_back(CandidateEdge{comparisonIndex, referenceIndex, distance});
							}
						}
			}
			return edges;
		}

		// Non-periodic equivalent: a plain Cartesian cell list (bin edge length ==
		// maxMatchDisplacementAngstrom, own + all 26 neighbor bins, no wraparound - integer bins can
		// be negative, so an unordered_map keyed by a packed bin coordinate replaces the dense
		// fractional grid above). Used whenever periodic matching isn't valid (either structure has
		// isPeriodic == false, or the lattices don't match closely enough - see
		// LocalMatchingPlan::usePeriodicMatching) instead of pretending a periodic cell exists.
		[[nodiscard]] std::vector<CandidateEdge> BuildNonPeriodicCandidateEdges(
			const CrystalStructure &reference,
			const CrystalStructure &comparison,
			const ElementPropertiesTable &elementPropertiesTable,
			float maxMatchDisplacementAngstrom,
			float cutoffScale,
			bool restrictToSameSpecies)
		{
			std::vector<CandidateEdge> edges;
			const std::size_t referenceCount = reference.atoms.size();
			const std::size_t comparisonCount = comparison.atoms.size();
			if (referenceCount == 0 || comparisonCount == 0)
				return edges;

			const float binSize = std::max(maxMatchDisplacementAngstrom, 1e-3f);
			const auto binOf = [&](const glm::vec3 &position) {
				return glm::ivec3(
					static_cast<int>(std::floor(position.x / binSize)),
					static_cast<int>(std::floor(position.y / binSize)),
					static_cast<int>(std::floor(position.z / binSize)));
			};
			const auto packKey = [](const glm::ivec3 &bin) -> std::int64_t {
				constexpr std::int64_t kBias = 1 << 20;
				constexpr std::int64_t kBase = 1 << 21;
				return ((static_cast<std::int64_t>(bin.x) + kBias) * kBase +
						   (static_cast<std::int64_t>(bin.y) + kBias)) *
						kBase +
					(static_cast<std::int64_t>(bin.z) + kBias);
			};

			std::unordered_map<std::int64_t, std::vector<std::size_t>> referenceBins;
			for (std::size_t referenceIndex = 0; referenceIndex < referenceCount; ++referenceIndex)
				referenceBins[packKey(binOf(reference.atoms[referenceIndex].position))].push_back(referenceIndex);

			for (std::size_t comparisonIndex = 0; comparisonIndex < comparisonCount; ++comparisonIndex)
			{
				const AtomSite &comparisonAtom = comparison.atoms[comparisonIndex];
				const glm::ivec3 centerBin = binOf(comparisonAtom.position);

				for (int dx = -1; dx <= 1; ++dx)
					for (int dy = -1; dy <= 1; ++dy)
						for (int dz = -1; dz <= 1; ++dz)
						{
							const auto found = referenceBins.find(packKey(centerBin + glm::ivec3(dx, dy, dz)));
							if (found == referenceBins.end())
								continue;

							for (std::size_t referenceIndex : found->second)
							{
								const AtomSite &referenceAtom = reference.atoms[referenceIndex];
								if (restrictToSameSpecies && referenceAtom.species != comparisonAtom.species)
									continue;

								const float effectiveCutoff = EffectiveMatchCutoff(
									elementPropertiesTable, comparisonAtom, referenceAtom, maxMatchDisplacementAngstrom,
									cutoffScale);
								const glm::vec3 delta = referenceAtom.position - comparisonAtom.position;
								const float distance = glm::length(delta);
								if (distance <= effectiveCutoff)
									edges.push_back(CandidateEdge{comparisonIndex, referenceIndex, distance});
							}
						}
			}
			return edges;
		}

		// Minimal union-find (path compression, no union-by-rank - components here are at most a
		// handful of atoms, not worth the extra bookkeeping).
		class UnionFind
		{
		public:
			explicit UnionFind(std::size_t count) : m_Parent(count)
			{
				std::iota(m_Parent.begin(), m_Parent.end(), std::size_t{0});
			}

			std::size_t Find(std::size_t index)
			{
				while (m_Parent[index] != index)
				{
					m_Parent[index] = m_Parent[m_Parent[index]];
					index = m_Parent[index];
				}
				return index;
			}

			void Union(std::size_t a, std::size_t b)
			{
				a = Find(a);
				b = Find(b);
				if (a != b)
					m_Parent[a] = b;
			}

		private:
			std::vector<std::size_t> m_Parent;
		};
	} // namespace

	LocalMatchingPlan BuildLocalMatchingPlan(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const ElementPropertiesTable &elementPropertiesTable,
		float maxMatchDisplacementAngstrom,
		float cutoffScale,
		bool restrictToSameSpecies)
	{
		LocalMatchingPlan plan;

		// Periodic matching is only valid when BOTH structures actually claim to be periodic AND
		// their lattices agree - never apply minimum-image wrapping just because two LatticeCell
		// objects happen to match while one/both structures are marked non-periodic (e.g. an
		// isolated molecule/cluster loaded into a nominal unit box).
		const bool periodicComparisonRequested = reference.isPeriodic && comparison.isPeriodic;
		const bool latticeMismatch =
			periodicComparisonRequested && !LatticesMatch(reference.cell.ToMatrix(), comparison.cell.ToMatrix());
		plan.usePeriodicMatching = periodicComparisonRequested && !latticeMismatch;
		if (latticeMismatch)
			plan.latticeMismatchWarning = MakeLatticeMismatchWarning();

		const std::size_t referenceCount = reference.atoms.size();
		const std::size_t comparisonCount = comparison.atoms.size();

		std::vector<CandidateEdge> edges = plan.usePeriodicMatching
			? BuildPeriodicCandidateEdges(
				  reference, comparison, elementPropertiesTable, maxMatchDisplacementAngstrom, cutoffScale,
				  restrictToSameSpecies)
			: BuildNonPeriodicCandidateEdges(
				  reference, comparison, elementPropertiesTable, maxMatchDisplacementAngstrom, cutoffScale,
				  restrictToSameSpecies);
		plan.candidateEdgeCount = edges.size();

		// Union-find over comparisonCount + referenceCount nodes: comparison atoms occupy
		// [0, comparisonCount), reference atoms occupy [comparisonCount, comparisonCount +
		// referenceCount). Spatial bins above are ONLY candidate-generation acceleration - an atom
		// near a bin boundary can have candidates spanning multiple bins, so independently solving
		// per-bin could assign the same atom twice. Connected components (this union-find), not
		// bins, define the actual independent local assignment problems.
		UnionFind unionFind(comparisonCount + referenceCount);
		for (const CandidateEdge &edge : edges)
			unionFind.Union(edge.comparisonIndex, comparisonCount + edge.referenceIndex);

		struct ComponentBuild
		{
			std::vector<std::size_t> comparisonAtomIndices;
			std::vector<std::size_t> referenceAtomIndices;
		};
		std::unordered_map<std::size_t, std::size_t> rootToComponentBuildIndex;
		std::vector<ComponentBuild> componentBuilds;
		const auto componentBuildFor = [&](std::size_t root) -> ComponentBuild & {
			const auto [iterator, inserted] = rootToComponentBuildIndex.try_emplace(root, componentBuilds.size());
			if (inserted)
				componentBuilds.emplace_back();
			return componentBuilds[iterator->second];
		};
		// Every atom is grouped by its component root - including ones with zero candidate edges,
		// which end up alone in their own singleton component (trivially unmatched below).
		for (std::size_t comparisonIndex = 0; comparisonIndex < comparisonCount; ++comparisonIndex)
			componentBuildFor(unionFind.Find(comparisonIndex)).comparisonAtomIndices.push_back(comparisonIndex);
		for (std::size_t referenceIndex = 0; referenceIndex < referenceCount; ++referenceIndex)
			componentBuildFor(unionFind.Find(comparisonCount + referenceIndex))
				.referenceAtomIndices.push_back(referenceIndex);

		for (ComponentBuild &build : componentBuilds)
		{
			if (build.comparisonAtomIndices.empty())
			{
				plan.isolatedReferenceIndices.insert(
					plan.isolatedReferenceIndices.end(), build.referenceAtomIndices.begin(),
					build.referenceAtomIndices.end());
				continue;
			}
			if (build.referenceAtomIndices.empty())
			{
				plan.isolatedComparisonIndices.insert(
					plan.isolatedComparisonIndices.end(), build.comparisonAtomIndices.begin(),
					build.comparisonAtomIndices.end());
				continue;
			}

			plan.largestComponentSize = std::max(
				plan.largestComponentSize, build.comparisonAtomIndices.size() + build.referenceAtomIndices.size());

			LocalMatchingComponent component;
			component.comparisonAtomIndices = std::move(build.comparisonAtomIndices);
			component.referenceAtomIndices = std::move(build.referenceAtomIndices);
			component.costMatrix.comparisonCount = component.comparisonAtomIndices.size();
			component.costMatrix.referenceCount = component.referenceAtomIndices.size();
			component.costMatrix.costs.assign(
				component.costMatrix.comparisonCount * component.costMatrix.referenceCount,
				kUnmatchedDisplacementCost);
			plan.assignableComponents.push_back(std::move(component));
		}

		// Fill each assignable component's local matrix from the edges that belong to it. Global ->
		// (componentIndex, localIndex) lookups translate each edge's global atom indices into local
		// matrix coordinates.
		std::unordered_map<std::size_t, std::pair<std::size_t, std::size_t>> comparisonAtomLocation;
		std::unordered_map<std::size_t, std::pair<std::size_t, std::size_t>> referenceAtomLocation;
		for (std::size_t componentIndex = 0; componentIndex < plan.assignableComponents.size(); ++componentIndex)
		{
			const LocalMatchingComponent &component = plan.assignableComponents[componentIndex];
			for (std::size_t local = 0; local < component.comparisonAtomIndices.size(); ++local)
				comparisonAtomLocation[component.comparisonAtomIndices[local]] = {componentIndex, local};
			for (std::size_t local = 0; local < component.referenceAtomIndices.size(); ++local)
				referenceAtomLocation[component.referenceAtomIndices[local]] = {componentIndex, local};
		}
		for (const CandidateEdge &edge : edges)
		{
			const auto comparisonLocation = comparisonAtomLocation.find(edge.comparisonIndex);
			const auto referenceLocation = referenceAtomLocation.find(edge.referenceIndex);
			// Defensive only: union-find guarantees any real edge's two endpoints share a component,
			// so both lookups above always succeed and land in the same componentIndex.
			if (comparisonLocation == comparisonAtomLocation.end() || referenceLocation == referenceAtomLocation.end())
				continue;
			if (comparisonLocation->second.first != referenceLocation->second.first)
				continue;

			LocalMatchingComponent &component = plan.assignableComponents[comparisonLocation->second.first];
			const std::size_t localComparison = comparisonLocation->second.second;
			const std::size_t localReference = referenceLocation->second.second;
			const std::size_t costIndex = localComparison * component.costMatrix.referenceCount + localReference;
			component.costMatrix.costs[costIndex] = std::min(component.costMatrix.costs[costIndex], edge.distance);
		}

		return plan;
	}

	StructureComparisonResult BuildComparisonResultFromLocalPlan(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const LocalMatchingPlan &plan,
		std::span<const std::vector<int>> componentAssignments)
	{
		StructureComparisonResult result;
		result.latticeMismatchWarning = plan.latticeMismatchWarning;

		std::vector<bool> referenceMatched(reference.atoms.size(), false);
		const glm::mat3 latticeMatrix = reference.cell.ToMatrix();

		const auto reportUnmatchedComparison = [&](std::size_t globalComparisonIndex) {
			const AtomSite &comparisonAtom = comparison.atoms[globalComparisonIndex];
			result.unmatchedComparisonAtoms.push_back(UnmatchedComparisonAtom{comparisonAtom.position, comparisonAtom.species});
		};

		const std::size_t componentCount = std::min(componentAssignments.size(), plan.assignableComponents.size());
		for (std::size_t componentIndex = 0; componentIndex < componentCount; ++componentIndex)
		{
			const LocalMatchingComponent &component = plan.assignableComponents[componentIndex];
			const std::vector<int> &assignment = componentAssignments[componentIndex];

			for (std::size_t localComparison = 0; localComparison < component.comparisonAtomIndices.size();
				 ++localComparison)
			{
				const std::size_t globalComparisonIndex = component.comparisonAtomIndices[localComparison];
				const int localReference = localComparison < assignment.size() ? assignment[localComparison] : -1;
				const bool isAssigned = localReference >= 0 &&
					static_cast<std::size_t>(localReference) < component.referenceAtomIndices.size();
				const float cost = isAssigned
					? component.costMatrix.At(localComparison, static_cast<std::size_t>(localReference))
					: kUnmatchedDisplacementCost;

				if (isAssigned && cost < kUnmatchedDisplacementCost)
				{
					const std::size_t globalReferenceIndex =
						component.referenceAtomIndices[static_cast<std::size_t>(localReference)];
					referenceMatched[globalReferenceIndex] = true;

					AtomDisplacement displacement;
					displacement.referenceAtomIndex = globalReferenceIndex;
					displacement.comparisonAtomIndex = globalComparisonIndex;
					displacement.referencePosition = reference.atoms[globalReferenceIndex].position;
					displacement.comparisonPosition = comparison.atoms[globalComparisonIndex].position;
					displacement.comparisonPositionWrapped = displacement.referencePosition +
						DisplacementDelta(
							plan.usePeriodicMatching, latticeMatrix, displacement.comparisonPosition,
							displacement.referencePosition);
					displacement.magnitudeAngstrom = cost;
					result.matches.push_back(displacement);
				}
				else
				{
					reportUnmatchedComparison(globalComparisonIndex);
				}
			}
		}

		// A componentAssignments shorter than plan.assignableComponents (caller supplied fewer
		// solved matrices than components) must not silently drop the remaining components' atoms -
		// every one of them is explicitly unmatched instead.
		for (std::size_t componentIndex = componentCount; componentIndex < plan.assignableComponents.size();
			 ++componentIndex)
			for (std::size_t globalComparisonIndex : plan.assignableComponents[componentIndex].comparisonAtomIndices)
				reportUnmatchedComparison(globalComparisonIndex);

		for (std::size_t globalComparisonIndex : plan.isolatedComparisonIndices)
			reportUnmatchedComparison(globalComparisonIndex);

		// isolatedReferenceIndices needs no separate handling - referenceMatched starts false for
		// every reference atom (isolated ones included) and this final sweep collects every index
		// that never got matched above, exactly the same closing pattern the old whole-structure
		// version used.
		for (std::size_t referenceIndex = 0; referenceIndex < reference.atoms.size(); ++referenceIndex)
			if (!referenceMatched[referenceIndex])
				result.unmatchedReferenceAtomIndices.push_back(referenceIndex);

		return result;
	}
} // namespace DefectStudio
