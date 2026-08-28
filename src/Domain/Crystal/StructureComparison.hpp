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
	// before/after a substitution changes atom count) - see BuildComparisonResultFromAssignment.
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

	// comparisonCount x referenceCount row-major cost matrix (cost = minimum-image distance in
	// Angstrom, or a sentinel above cutoffAngstrom for pairs that shouldn't be matched). Intended
	// to be handed to an optimal bipartite assignment solver (e.g. scipy.optimize.linear_sum_assignment
	// via ScipyAssignmentBridge) - building this matrix and consuming its assignment are kept as two
	// separate pure functions here so both are unit-testable without a subprocess.
	// Cost written for a pair whose distance exceeds its cutoff (species-pair-dependent, so not a
	// single scalar the matrix can carry) - deliberately far outside any real minimum-image
	// distance a crystal structure could produce, so a solver only assigns it when forced to
	// (rectangular assignment must map every row somewhere) and BuildComparisonResultFromAssignment
	// can unambiguously recognize and discard those forced-bad pairings.
	inline constexpr float kUnmatchedDisplacementCost = 1.0e6f;

	struct DisplacementCostMatrix
	{
		std::vector<float> costs;
		std::size_t comparisonCount = 0;
		std::size_t referenceCount = 0;
		// Set when the two structures' lattices differ beyond tolerance. The matrix is still built
		// (minimum-image distance computed against the reference lattice, an approximation rather
		// than a hard error - the user picks which files to compare, so this is a heads-up, not a
		// block) - BuildComparisonResultFromAssignment turns this into StructureComparisonResult::
		// latticeMismatchWarning for the UI to show.
		bool latticeMismatch = false;

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
		// Non-empty = show as a UI warning (not an error - see DisplacementCostMatrix::latticeMismatch).
		std::string latticeMismatchWarning;
	};

	// Pure, sync. Builds the cross-structure distance matrix using the generalized minimum-image
	// convention (PeriodicGeometry.hpp) instead of raw Cartesian distance, so pairs across a
	// periodic cell boundary aren't reported as spuriously far apart. Species filtering is opt-in
	// (restrictToSameSpecies=false matches the 2026-08-24 decision: cross-species matching, e.g.
	// C->B, is ON by default - the caller controls what's actually drawn via a display threshold
	// applied to the result later, not by refusing to match here). Mismatched lattices no longer
	// fail this call (see DisplacementCostMatrix::latticeMismatch) - the Result is kept for API
	// stability / future validation, not because this can fail today.
	[[nodiscard]] Result<DisplacementCostMatrix> BuildDisplacementCostMatrix(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const ElementPropertiesTable &elementPropertiesTable,
		float cutoffScale = 1.5f,
		bool restrictToSameSpecies = false);

	// Pure, sync. Turns a raw assignment (comparisonToReferenceAssignment[comparisonIndex] =
	// referenceIndex, or a negative value for "unassigned") back into the three result buckets.
	// An assigned pair whose original cost was the sentinel (kUnmatchedDisplacementCost - a
	// forced-bad pairing that only happened because a rectangular assignment must map every row
	// somewhere) is treated as unmatched on both sides, not as a real (bogus, very long) arrow.
	[[nodiscard]] StructureComparisonResult BuildComparisonResultFromAssignment(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const DisplacementCostMatrix &costMatrix,
		std::span<const int> comparisonToReferenceAssignment);
} // namespace DefectStudio
