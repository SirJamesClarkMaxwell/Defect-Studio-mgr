#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>
#include <vector>

#include <glm/glm.hpp>

namespace DefectStudio::detail
{
	// Ear clipping keeps every triangle inside a simple concave face. A centre fan cannot do that
	// for the U-shaped top face produced when a curved shaft is united with its decoration.
	inline bool TriangulateSimplePolygon(const std::vector<glm::dvec3> &positions,
		const glm::dvec3 &normal, std::vector<std::array<std::size_t, 3u>> &triangles)
	{
		triangles.clear();
		if (positions.size() < 3u)
			return false;

		double scaleSquared = 0.0;
		for (std::size_t index = 0u; index < positions.size(); ++index)
			scaleSquared = std::max(scaleSquared, glm::dot(positions[(index + 1u) % positions.size()] - positions[index],
				positions[(index + 1u) % positions.size()] - positions[index]));
		const double epsilon = std::max(1.0e-20, scaleSquared * 1.0e-12);
		const auto orientation = [&](const std::size_t first, const std::size_t second, const std::size_t third) {
			return glm::dot(glm::cross(positions[second] - positions[first], positions[third] - positions[second]), normal);
		};
		const auto insideOrOn = [&](const glm::dvec3 &point, const std::size_t first,
			const std::size_t second, const std::size_t third) {
			return glm::dot(glm::cross(positions[second] - positions[first], point - positions[first]), normal) >= -epsilon &&
				glm::dot(glm::cross(positions[third] - positions[second], point - positions[second]), normal) >= -epsilon &&
				glm::dot(glm::cross(positions[first] - positions[third], point - positions[third]), normal) >= -epsilon;
		};

		std::vector<std::size_t> remaining(positions.size());
		std::iota(remaining.begin(), remaining.end(), 0u);
		while (remaining.size() > 3u)
		{
			bool clipped = false;
			for (std::size_t index = 0u; index < remaining.size(); ++index)
			{
				const std::size_t previous = remaining[(index + remaining.size() - 1u) % remaining.size()];
				const std::size_t current = remaining[index];
				const std::size_t next = remaining[(index + 1u) % remaining.size()];
				if (orientation(previous, current, next) <= epsilon)
					continue;
				// With one vertex left to clip, do not choose an otherwise valid ear that
				// leaves three collinear boundary points. Another corner still yields the
				// same polygon area while preserving every original boundary segment.
				if (remaining.size() == 4u)
				{
					std::array<std::size_t, 3u> final{};
					std::size_t finalIndex = 0u;
					for (const std::size_t candidate : remaining)
						if (candidate != current)
							final[finalIndex++] = candidate;
					if (orientation(final[0], final[1], final[2]) <= epsilon)
						continue;
				}
				bool containsVertex = false;
				for (const std::size_t candidate : remaining)
					if (candidate != previous && candidate != current && candidate != next &&
						insideOrOn(positions[candidate], previous, current, next))
					{
						containsVertex = true;
						break;
					}
				if (containsVertex)
					continue;
				triangles.push_back({previous, current, next});
				remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));
				clipped = true;
				break;
			}
			if (!clipped)
				return false;
		}
		if (orientation(remaining[0], remaining[1], remaining[2]) <= epsilon)
			return false;
		triangles.push_back({remaining[0], remaining[1], remaining[2]});
		return triangles.size() + 2u == positions.size();
	}
}
