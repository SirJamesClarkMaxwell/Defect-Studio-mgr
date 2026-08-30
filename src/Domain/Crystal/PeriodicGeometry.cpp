#include "Core/dspch.hpp"

#include "Domain/Crystal/PeriodicGeometry.hpp"

#include <glm/geometric.hpp>

namespace DefectStudio
{
	std::array<glm::ivec3, 26> BuildNonZeroLatticeShifts()
	{
		std::array<glm::ivec3, 26> shifts{};
		std::size_t index = 0;
		for (int dx = -1; dx <= 1; ++dx)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dz = -1; dz <= 1; ++dz)
				{
					if (dx == 0 && dy == 0 && dz == 0)
						continue;
					shifts[index] = glm::ivec3(dx, dy, dz);
					++index;
				}
		return shifts;
	}

	glm::vec3 CartesianShift(const glm::mat3 &latticeMatrix, const glm::ivec3 &shift)
	{
		return latticeMatrix[0] * static_cast<float>(shift.x) +
			latticeMatrix[1] * static_cast<float>(shift.y) +
			latticeMatrix[2] * static_cast<float>(shift.z);
	}

	glm::vec3 MinimumImageCartesianDelta(
		const glm::mat3 &latticeMatrix,
		const glm::vec3 &a,
		const glm::vec3 &b)
	{
		// Every candidate shift must be evaluated against the ORIGINAL (a - b), not against
		// whichever shift currently looks best - a previous bug here fed bestDelta back in as the
		// base for the next candidate, so later shifts were silently evaluated relative to an
		// already-shifted point instead of the true separation (confirmed 2026-08-28: this can pick
		// a wrong, non-minimal image on some inputs). baseDelta stays fixed for the whole search.
		const glm::vec3 baseDelta = a - b;
		glm::vec3 bestDelta = baseDelta;
		float bestDistanceSquared = glm::dot(bestDelta, bestDelta);

		static const std::array<glm::ivec3, 26> shifts = BuildNonZeroLatticeShifts();
		for (const glm::ivec3 &shift : shifts)
		{
			const glm::vec3 delta = baseDelta + CartesianShift(latticeMatrix, shift);
			const float distanceSquared = glm::dot(delta, delta);
			if (distanceSquared < bestDistanceSquared)
			{
				bestDistanceSquared = distanceSquared;
				bestDelta = delta;
			}
		}
		return bestDelta;
	}

	glm::vec3 FractionalSearchRadius(const glm::mat3 &inverseLatticeMatrix, float cartesianRadius)
	{
		// For a Cartesian displacement dr with |dr| <= cartesianRadius, its fractional counterpart
		// is df = inverseLatticeMatrix * dr. By Cauchy-Schwarz, |df_i| = |row_i . dr| <=
		// |row_i| * |dr| <= |row_i| * cartesianRadius, where row_i is row i of the inverse lattice
		// matrix (glm::mat3 is column-major, so row i is (M[0][i], M[1][i], M[2][i])). This bound
		// holds for any lattice shape (skewed/non-orthogonal included) - it is conservative (the
		// true reachable fractional range is generally smaller), which is exactly what a candidate
		// spatial search needs: never miss a real neighbor, possibly scan a few extra bins.
		glm::vec3 radius(0.0f);
		for (int axis = 0; axis < 3; ++axis)
		{
			const glm::vec3 row(
				inverseLatticeMatrix[0][axis], inverseLatticeMatrix[1][axis], inverseLatticeMatrix[2][axis]);
			radius[axis] = glm::length(row) * cartesianRadius;
		}
		return radius;
	}
} // namespace DefectStudio
