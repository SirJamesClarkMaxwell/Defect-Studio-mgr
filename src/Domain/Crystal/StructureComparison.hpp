#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	// One matched atom pair between two possibly differently-composed structures (e.g. a defect
	// before/after a substitution changes atom count) - see BuildComparisonResultFromLocalPlan.
	// Two consumers: displacement-arrow rendering (referenceAtomIndex anchors the arrow to a live
	// atom in the already-open reference window instead of storing a second raw position for it)
	// and SceneOutlinerPanel's "Copy view + visibility to..." (needs comparisonAtomIndex too, since
	// both windows there are already open and indexable - a raw position can't be looked back up
	// into a specific atom the way it can for a not-necessarily-open comparison file).
	struct AtomDisplacement
	{
		std::size_t referenceAtomIndex = 0;
		std::size_t comparisonAtomIndex = 0;
		glm::vec3 referencePosition = glm::vec3(0.0f);
		glm::vec3 comparisonPosition = glm::vec3(0.0f);
		// referencePosition + the minimum-image delta to comparisonPosition - what drawing code
		// should use for the arrow's tip instead of the raw comparisonPosition. Raw positions can be
		// many cells apart in absolute coordinates even when the atom only moved a short distance
		// across a periodic boundary (drawing raw comparisonPosition would span the whole cell); this
		// field is the "short way around" point, which may sit just outside the cell box - that's
		// the intended "atom exits the cell" look, not a bug.
		glm::vec3 comparisonPositionWrapped = glm::vec3(0.0f);
		float magnitudeAngstrom = 0.0f;
	};

	// A comparison-structure atom with no valid reference counterpart (present in B, absent in A -
	// interstitial-like). Carries its own position/species since it has no reference atom to borrow
	// them from.
	struct UnmatchedComparisonAtom
	{
		glm::vec3 position = glm::vec3(0.0f);
		std::string species;
	};

	// comparisonCount x referenceCount row-major cost matrix (cost = displacement distance in
	// Angstrom, or a sentinel for pairs that shouldn't be matched). Sized to a single LOCAL
	// connected component's atoms (LocalMatchingComponent below), not the whole structure - see
	// "2026-08-28: local spatial matching" at the top of StructureComparison.cpp for why a global
	// N x M matrix is no longer built. Cost written for a pair with no candidate edge (too far, or
	// simply not in the same component) - deliberately far outside any real matching distance, so a
	// solver only assigns it when structurally forced to (see BuildComparisonResultFromLocalPlan)
	// and that function can unambiguously recognize and discard those forced-bad pairings.
	inline constexpr float kUnmatchedDisplacementCost = 1.0e6f;

	struct DisplacementCostMatrix
	{
		std::vector<float> costs;
		std::size_t comparisonCount = 0;
		std::size_t referenceCount = 0;

		[[nodiscard]] float At(std::size_t comparisonIndex, std::size_t referenceIndex) const
		{
			return costs[comparisonIndex * referenceCount + referenceIndex];
		}
	};

	struct StructureComparisonResult
	{
		std::vector<AtomDisplacement> matches;
		std::vector<std::size_t> unmatchedReferenceAtomIndices; // vacancy-like (in A, no match in B)
		std::vector<UnmatchedComparisonAtom> unmatchedComparisonAtoms; // interstitial-like (in B, no match in A)
		// Non-empty = show as a UI warning (not an error - see LocalMatchingPlan::latticeMismatchWarning).
		std::string latticeMismatchWarning;
	};

	// One local, spatially-connected group of atoms that could plausibly match each other -
	// comparisonAtomIndices/referenceAtomIndices are GLOBAL atom indices into `comparison`/
	// `reference`. Both non-empty (see BuildLocalMatchingPlan) - a component with only one side
	// is trivial (no candidate at all) and is reported directly via
	// LocalMatchingPlan::isolatedReferenceIndices/isolatedComparisonIndices instead, needing no
	// solver. costMatrix is LOCAL: comparisonCount == comparisonAtomIndices.size(), referenceCount
	// == referenceAtomIndices.size() - hand it to ScipyAssignmentBridge like any other
	// DisplacementCostMatrix, then map the returned local assignment back through the index arrays.
	struct LocalMatchingComponent
	{
		std::vector<std::size_t> comparisonAtomIndices;
		std::vector<std::size_t> referenceAtomIndices;
		DisplacementCostMatrix costMatrix;
	};

	// Output of the local-matching candidate search (BuildLocalMatchingPlan): the whole matching
	// problem broken into independent, spatially-local pieces instead of one global comparisonCount
	// x referenceCount problem. See BuildComparisonResultFromLocalPlan for how this - plus one
	// Hungarian assignment per assignableComponents entry - becomes a StructureComparisonResult.
	struct LocalMatchingPlan
	{
		// Components with candidate atoms on BOTH sides - each needs one Hungarian solve
		// (assignableComponents.size() == "number of Hungarian subproblems").
		std::vector<LocalMatchingComponent> assignableComponents;
		// Reference atoms with zero candidate comparison atoms nearby - vacancy-like without needing
		// a solver at all.
		std::vector<std::size_t> isolatedReferenceIndices;
		// Comparison atoms with zero candidate reference atoms nearby - interstitial-like without
		// needing a solver at all.
		std::vector<std::size_t> isolatedComparisonIndices;
		// true if periodic minimum-image matching was used (both structures isPeriodic and lattices
		// match within tolerance); false = plain non-periodic Cartesian matching, either because
		// either structure isn't periodic or because the lattices differ (see latticeMismatchWarning).
		bool usePeriodicMatching = false;
		// Non-empty only when periodic matching was REQUESTED (both isPeriodic) but the lattices
		// differ beyond tolerance - see StructureComparisonResult::latticeMismatchWarning.
		std::string latticeMismatchWarning;
		// Debug/verification stats (2026-08-28 spec: confirm a 1000-atom structure doesn't secretly
		// become a global 1000x1000 problem) - not used by matching logic itself.
		std::size_t candidateEdgeCount = 0;
		std::size_t largestComponentSize = 0;
	};

	// Pure, sync. Finds, for every comparison atom, only the nearby reference atoms it could
	// plausibly correspond to (periodic spatial binning when usable, else a non-periodic Cartesian
	// cell list - see .cpp), then partitions the resulting bipartite candidate graph into connected
	// components via union-find. This is what replaces the old global dense cost matrix: a
	// vacancy/substitution's Hungarian permutation is now confined to its own local component
	// instead of being free to "trade" cost across the entire structure (the 2026-08-28 "arrows
	// zigzagging through several hexagon rings" bug).
	//
	// maxMatchDisplacementAngstrom is the identity bound: "a comparison atom farther than this from
	// a reference atom cannot be the same physical atom after relaxation". This is intentionally
	// separate from any display/UI threshold (DisplacementComparisonState::displayThresholdAngstrom)
	// - that one only filters what's DRAWN, after matching already happened; this one decides
	// matching itself. The old covalent-radius-based cutoff (cutoffScale * sum of radii) is kept as
	// an ADDITIONAL, tighter upper bound where it happens to be smaller (light-element pairs), but
	// no longer the only identity criterion - for heavy elements its radius sum alone could exceed a
	// full lattice spacing, which is exactly what let far/wrong atoms match before.
	inline constexpr float kDefaultMaxMatchDisplacementAngstrom = 2.0f;

	[[nodiscard]] LocalMatchingPlan BuildLocalMatchingPlan(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const ElementPropertiesTable &elementPropertiesTable,
		float maxMatchDisplacementAngstrom = kDefaultMaxMatchDisplacementAngstrom,
		float cutoffScale = 1.5f,
		bool restrictToSameSpecies = false);

	// Pure, sync. Turns a solved LocalMatchingPlan - one Hungarian assignment per
	// plan.assignableComponents entry, same order, each sized like that component's costMatrix -
	// into the three StructureComparisonResult buckets. Each componentAssignments[i] uses the same
	// convention as a single-matrix assignment (LOCAL comparisonIndex -> LOCAL referenceIndex, or a
	// negative/out-of-range value for "unassigned"); a pair whose local cost is the sentinel
	// (kUnmatchedDisplacementCost - forced only because a component's two sides couldn't be
	// perfectly matched with real edges alone, see BuildLocalMatchingPlan) is treated as unmatched
	// on both sides, not as a real (bogus) displacement. A componentAssignments[i] shorter than its
	// component's comparisonAtomIndices never silently drops the missing comparison atoms - anything
	// past the supplied assignment's length is explicitly unmatched too.
	[[nodiscard]] StructureComparisonResult BuildComparisonResultFromLocalPlan(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const LocalMatchingPlan &plan,
		std::span<const std::vector<int>> componentAssignments);
} // namespace DefectStudio
