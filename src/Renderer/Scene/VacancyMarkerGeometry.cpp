#include "Core/dspch.hpp"

#include "Renderer/Scene/VacancyMarkerGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace DefectStudio
{
	VacancyMarkerMesh BuildVacancyMarkerMesh(
		const RendererVacancyData &vacancy, const glm::vec3 &cameraRight,
		const glm::vec3 &cameraUp, float ringWidth)
	{
		VacancyMarkerMesh mesh;
		if (!(vacancy.radius > 0.0f) || !(ringWidth > 0.0f) ||
			!std::isfinite(vacancy.radius) || !std::isfinite(ringWidth))
			return mesh;
		const float width = std::min(ringWidth, vacancy.radius);
		const glm::vec3 normal = glm::normalize(glm::cross(cameraRight, cameraUp));
		const auto point = [&](float radius, float angle) {
			return vacancy.cartesianPosition + radius *
				(std::cos(angle) * cameraRight + std::sin(angle) * cameraUp);
		};
		if (vacancy.renderMode != VacancyRenderMode::Wireframe)
		{
			mesh.fill.reserve(kVacancyDiscSegments * 3);
			const float radius = vacancy.radius - width * 0.5f;
			for (int i = 0; i < kVacancyDiscSegments; ++i)
			{
				mesh.fill.push_back({vacancy.cartesianPosition, normal, 1.0f});
				mesh.fill.push_back({point(radius, glm::two_pi<float>() * i / kVacancyDiscSegments), normal, 1.0f});
				mesh.fill.push_back({point(radius, glm::two_pi<float>() * (i + 1) / kVacancyDiscSegments), normal, 1.0f});
			}
		}
		const int dashes = std::clamp(vacancy.dashCount, 0, kVacancyMaxDashCount);
		const int count = dashes == 0 ? 1 : dashes;
		const int subdivisions = dashes == 0 ? kVacancySolidRingSegments : kVacancyDashSubdivisions;
		const float period = glm::two_pi<float>() / count;
		const float step = period * (dashes == 0 ? 1.0f : 0.5f) / subdivisions;
		mesh.ring.reserve(count * subdivisions * 6);
		for (int dash = 0; dash < count; ++dash)
		{
			for (int i = 0; i < subdivisions; ++i)
			{
				const float start = dash * period + i * step;
				const float end = dash * period + (i + 1) * step;
				const glm::vec3 a = point(vacancy.radius, start);
				const glm::vec3 b = point(vacancy.radius, end);
				const glm::vec3 c = point(vacancy.radius - width, end);
				const glm::vec3 d = point(vacancy.radius - width, start);
				for (const glm::vec3 &position : {a, b, c, a, c, d})
					mesh.ring.push_back({position, normal, -1.0f});
			}
		}
		return mesh;
	}
} // namespace DefectStudio
