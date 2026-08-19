#include "Core/dspch.hpp"

#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio::SelectionHitTest
{
	std::optional<glm::vec2> ProjectToScreen(
		const glm::mat4 &viewProjection, glm::vec2 viewportSize, const glm::vec3 &worldPosition)
	{
		const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
		if (clip.w <= 0.0001f)
			return std::nullopt;
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		return glm::vec2(
			(ndc.x + 1.0f) * 0.5f * viewportSize.x,
			(1.0f - ndc.y) * 0.5f * viewportSize.y);
	}

	bool PointInRect(glm::vec2 point, glm::vec2 rectMin, glm::vec2 rectMax)
	{
		return point.x >= rectMin.x && point.x <= rectMax.x &&
			point.y >= rectMin.y && point.y <= rectMax.y;
	}

	bool PointInCircle(glm::vec2 point, glm::vec2 center, float radius)
	{
		return glm::length(point - center) <= radius;
	}
} // namespace DefectStudio::SelectionHitTest
