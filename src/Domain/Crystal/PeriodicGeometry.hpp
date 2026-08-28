#pragma once

#include <array>

#include <glm/glm.hpp>

namespace DefectStudio
{
	// The 26 non-zero neighbor-cell lattice-vector shift combinations (dx,dy,dz in {-1,0,1},
	// excluding (0,0,0)) - shared by BondGenerator's periodic bond search and
	// MinimumImageCartesianDelta below, so both agree on what "the 27-cell neighborhood" means.
	[[nodiscard]] std::array<glm::ivec3, 26> BuildNonZeroLatticeShifts();

	// Converts a lattice-vector shift (in units of whole cells) to a Cartesian offset.
	[[nodiscard]] glm::vec3 CartesianShift(const glm::mat3 &latticeMatrix, const glm::ivec3 &shift);

	// Generalized minimum-image convention: tries the zero shift plus all 26 neighbor-cell shifts
	// and returns the Cartesian delta (a - b) with the smallest norm. Robust for skewed
	// (non-orthogonal) cells, unlike the textbook fractional-wrap-to-[-0.5,0.5) formula, which only
	// holds for cells close to orthogonal. Same 27-shift search BondGenerator already uses for
	// periodic bonding (BuildPeriodicPotentialBondPairs), just as a direct point-pair query instead
	// of a cutoff-radius neighbor search.
	[[nodiscard]] glm::vec3 MinimumImageCartesianDelta(
		const glm::mat3 &latticeMatrix,
		const glm::vec3 &a,
		const glm::vec3 &b);
} // namespace DefectStudio
