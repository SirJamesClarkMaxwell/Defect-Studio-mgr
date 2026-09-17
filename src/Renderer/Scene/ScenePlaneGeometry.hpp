#pragma once

#include <array>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// Fitting a drawn plane through whatever the user had selected, and turning one back into the
	// four corners the renderer draws. Separate from the panel that calls it because "which plane
	// do three atoms define" is arithmetic with a right answer, and arithmetic with a right answer
	// is worth a test.

	struct ScenePlaneFit
	{
		glm::vec3 center = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f);
		glm::vec3 tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		// Sized to cover the fitted points with a margin, so the quad the user gets already frames
		// what they picked instead of needing to be dragged out to it.
		glm::vec2 halfExtents = glm::vec2(1.0f);
	};

	// Least-squares plane through `points` (the smallest-eigenvalue direction of their covariance),
	// centred on their centroid and oriented so `normal` points toward the viewer.
	//
	// Fewer than three points, or points that are collinear, do not determine a plane. Rather than
	// refusing - two atoms is exactly the case the user asks for most - the plane is taken to
	// contain the points and to face `viewDirection` as squarely as it can: the one plane through
	// that line which is actually legible from where the camera is standing. `tangent` then runs
	// along the points.
	//
	// Returns nullopt only when there is nothing to fit at all: no points, or every point at the
	// same position.
	[[nodiscard]] std::optional<ScenePlaneFit> FitScenePlane(
		const std::vector<glm::vec3> &points, const glm::vec3 &viewDirection);

	// The quad's four corners in world space, counter-clockwise seen from +normal, starting at
	// centre - tangent * halfExtents.x - bitangent * halfExtents.y.
	[[nodiscard]] std::array<glm::vec3, 4> ScenePlaneCorners(
		const RendererWindowState::ScenePlane &plane);

	// A plane built from a fit, with the drawing defaults filled in. The one place a new plane is
	// constructed, so the menu, the toolbar and any future command cannot disagree about what a
	// fresh plane looks like.
	[[nodiscard]] RendererWindowState::ScenePlane MakeScenePlane(const ScenePlaneFit &fit);
} // namespace DefectStudio
