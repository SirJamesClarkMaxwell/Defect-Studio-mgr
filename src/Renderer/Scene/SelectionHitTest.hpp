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

	// Clamped-t projection of point onto segment [segA, segB], then distance to that closest point -
	// degenerate (zero-length) segment falls back to point-to-segA distance. Screen-space pixel math,
	// shared by SceneArrow shaft picking (RendererPanel::handleSceneArrowInteraction) and its region
	// select (hitTestRectSceneArrows/hitTestCircleSceneArrows), and by DistancePointToTriangle2D below.
	[[nodiscard]] float DistancePointToSegment(glm::vec2 point, glm::vec2 segA, glm::vec2 segB);

	// Sign-of-cross-product test against all three edges (winding-independent - works whether a/b/c
	// are wound clockwise or counter-clockwise on screen).
	[[nodiscard]] bool PointInTriangle(glm::vec2 point, glm::vec2 a, glm::vec2 b, glm::vec2 c);

	// 0 if point is inside or on the triangle, else the distance to its nearest edge. Used for
	// Arrow2D's head hit-test - the head is a filled triangle on screen (see arrow_quad.frag), so
	// picking it should match that shape, not a bounding circle.
	[[nodiscard]] float DistancePointToTriangle2D(glm::vec2 point, glm::vec2 a, glm::vec2 b, glm::vec2 c);

	// Closest points between an infinite ray (rayOrigin + t*rayDir, rayDir normalized) and a finite
	// segment [segA, segB] - the segment parameter is clamped to [0,1], the ray parameter is left
	// unclamped (callers check outT > 0 for "in front of camera"). Used for bond hit-testing - a bond
	// has no analytic ray intersection like a sphere does, so picking it is "is the ray's closest
	// approach to this cylinder's axis within its radius".
	void ClosestPointsRaySegment(
		const glm::vec3 &rayOrigin, const glm::vec3 &rayDir, const glm::vec3 &segA, const glm::vec3 &segB,
		float &outT, glm::vec3 &outClosestOnSegment);
} // namespace DefectStudio::SelectionHitTest
