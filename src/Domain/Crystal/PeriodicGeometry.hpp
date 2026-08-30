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

	// Generalized minimum-image convention: tries the zero shift plus all 26 neighbor-cell (+-1 per
	// axis) shifts and returns the Cartesian delta (a - b) with the smallest norm. Correct for
	// skewed (non-orthogonal) cells as long as one +-1 shift per axis is enough to reach the true
	// nearest image - true for any cell where no single lattice vector's in-plane component exceeds
	// roughly half of it (ordinary crystallographic cells; NOT a proof for arbitrarily/pathologically
	// skewed reduced cells, where a correct search can in principle need shifts beyond +-1). Same
	// 27-shift search BondGenerator already uses for periodic bonding
	// (BuildPeriodicPotentialBondPairs), just as a direct point-pair query instead of a
	// cutoff-radius neighbor search.
	[[nodiscard]] glm::vec3 MinimumImageCartesianDelta(
		const glm::mat3 &latticeMatrix,
		const glm::vec3 &a,
		const glm::vec3 &b);

	// Conservative per-axis bound (in fractional-coordinate units) on how far a point within
	// cartesianRadius of another point can be from it along each lattice direction, derived from
	// the inverse lattice matrix's row norms (Cauchy-Schwarz) - see .cpp for the derivation. Used to
	// size a periodic spatial bin search so it never misses a real candidate, for any lattice shape
	// (not just near-cubic cells).
	[[nodiscard]] glm::vec3 FractionalSearchRadius(const glm::mat3 &inverseLatticeMatrix, float cartesianRadius);
} // namespace DefectStudio
