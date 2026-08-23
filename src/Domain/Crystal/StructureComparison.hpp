#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	// One matched atom pair between two possibly differently-composed structures (e.g. a defect
	// before/after a substitution changes atom count) - see ComputeAtomDisplacements. Intended
	// consumer: displacement-arrow rendering (docs/work/project/plans/
	// 2026-08-23-outliner-bonds-displacement.md, item 0) - referenceAtomIndex lets the renderer
	// anchor the arrow to a live atom in the already-open reference window instead of storing a
	// second raw position for it.
	struct AtomDisplacement
	{
		std::size_t referenceAtomIndex = 0;
		glm::vec3 referencePosition = glm::vec3(0.0f);
		glm::vec3 comparisonPosition = glm::vec3(0.0f);
		float magnitudeAngstrom = 0.0f;
	};

	// Matches atoms between two structures that aren't necessarily the same size/composition (a
	// plain "distance for pair i" like AtomsBase.get_distances in punktukas assumes a known index
	// correspondence, which doesn't hold here - see the plan doc's investigation, punktukas has no
	// built-in cross-structure atom-correspondence function to wrap). For each atom in `comparison`,
	// finds the nearest same-species atom in `reference` within cutoffScale * (sum of covalent
	// radii) - same cutoff shape as BondGenerator's auto-bond cutoff, so the two are visually and
	// numerically consistent. A comparison atom with no in-range same-species match (a vacancy, or a
	// substitution with no same-species neighbor nearby) is silently skipped, not an error - that's
	// the expected case, not a failure. Matched pairs that moved less than
	// minimumDisplacementAngstrom are also skipped, to avoid an arrow-per-atom on an otherwise
	// converged relaxation.
	//
	// ponytail: brute-force O(reference * comparison) distance search, no periodic-image wraparound
	// and no optimal (Hungarian-style) assignment - just "closest same-species reference atom, no
	// atom reused twice, closest match wins ties" (greedy, first-come-first-served in comparison
	// order). Fine for typical defect-supercell atom counts (tens to low thousands); revisit with a
	// spatial grid (BondGenerator's own bucketing) or scipy-style assignment if a real structure
	// exposes it as too slow or too greedy in practice.
	[[nodiscard]] std::vector<AtomDisplacement> ComputeAtomDisplacements(
		const CrystalStructure &reference,
		const CrystalStructure &comparison,
		const ElementPropertiesTable &elementPropertiesTable,
		float cutoffScale = 1.5f,
		float minimumDisplacementAngstrom = 0.02f);
} // namespace DefectStudio
