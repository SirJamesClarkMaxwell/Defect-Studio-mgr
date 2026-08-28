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
		glm::vec3 bestDelta = a - b;
		float bestDistanceSquared = glm::dot(bestDelta, bestDelta);

		static const std::array<glm::ivec3, 26> shifts = BuildNonZeroLatticeShifts();
		for (const glm::ivec3 &shift : shifts)
		{
			const glm::vec3 delta = bestDelta + CartesianShift(latticeMatrix, shift);
			const float distanceSquared = glm::dot(delta, delta);
			if (distanceSquared < bestDistanceSquared)
			{
				bestDistanceSquared = distanceSquared;
				bestDelta = delta;
			}
		}
		return bestDelta;
	}
} // namespace DefectStudio
