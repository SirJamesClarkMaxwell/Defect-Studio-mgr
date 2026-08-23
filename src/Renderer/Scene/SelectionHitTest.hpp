#pragma once

#include <optional>

#include <glm/glm.hpp>

namespace DefectStudio::SelectionHitTest
{
	// Projects a world-space point through a view-projection matrix into viewport pixel space
	// (origin top-left, y-down - matches ImGui mouse coordinates relative to the viewport image
	// origin, and is the exact inverse of RendererPanel::handleAtomPick's screen->NDC mapping).
	// Returns nullopt if the point projects behind the camera (w <= 0).
	[[nodiscard]] std::optional<glm::vec2> ProjectToScreen(
		const glm::mat4 &viewProjection, glm::vec2 viewportSize, const glm::vec3 &worldPosition);

	// rectMin/rectMax must already be normalized (rectMin <= rectMax component-wise) - callers
	// dragging in any direction are responsible for sorting the two drag corners first.
	[[nodiscard]] bool PointInRect(glm::vec2 point, glm::vec2 rectMin, glm::vec2 rectMax);

	[[nodiscard]] bool PointInCircle(glm::vec2 point, glm::vec2 center, float radius);

	// Closest points between an infinite ray (rayOrigin + t*rayDir, rayDir normalized) and a finite
	// segment [segA, segB] - the segment parameter is clamped to [0,1], the ray parameter is left
	// unclamped (callers check outT > 0 for "in front of camera"). Used for bond hit-testing - a bond
	// has no analytic ray intersection like a sphere does, so picking it is "is the ray's closest
	// approach to this cylinder's axis within its radius".
	void ClosestPointsRaySegment(
		const glm::vec3 &rayOrigin, const glm::vec3 &rayDir, const glm::vec3 &segA, const glm::vec3 &segB,
		float &outT, glm::vec3 &outClosestOnSegment);
} // namespace DefectStudio::SelectionHitTest
