#pragma once

#include <array>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/RendererViewCamera.hpp"
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

	// Ray against the drawn quads: the nearest visible plane the ray passes through, or nullopt if
	// it misses every one. Both faces count - a plane clicked from behind is still the object the
	// user meant. `rayDirection` need not be normalised.
	[[nodiscard]] std::optional<std::size_t> PickScenePlane(
		const RendererWindowState &windowState, const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection);

	// A plane built from a fit, with the drawing defaults filled in. The one place a new plane is
	// constructed, so the menu, the toolbar and any future command cannot disagree about what a
	// fresh plane looks like.
	[[nodiscard]] RendererWindowState::ScenePlane MakeScenePlane(const ScenePlaneFit &fit);

	// A free plane at `center` facing the viewer, for "add a plane" with no atoms selected. Sized
	// from the structure bounds, so it lands visible rather than as a
	// speck or a wall.
	[[nodiscard]] RendererWindowState::ScenePlane MakeDefaultScenePlane(
		const RendererWindowState &windowState, const glm::vec3 &center);

	// A screen-space pixel's worth of world units at `worldPoint`, measured by projecting a small
	// probe offset along `probeDirection` (need not be unit length) through `camera`'s own
	// view/projection - exact for both perspective (scales with that point's own depth, not the
	// orbit target's distance) and orthographic (constant everywhere). nullopt when the probe
	// lands behind the camera or the viewport has no extent.
	[[nodiscard]] std::optional<float> WorldUnitsPerPixelAt(
		const RendererViewCamera &camera, const glm::vec3 &worldPoint, const glm::vec3 &probeDirection,
		const glm::vec2 &viewportPixelSize);

	// The border/frame half-width the renderer should draw around `plane`, in world units. Starts
	// from the existing world-space proportion (a fraction of the plane's own half-extents) and
	// floors it, against both in-plane axes independently, so the border's on-screen width never
	// drops below a small minimum on either one. Without the floor, a plane seen edge-on has a
	// world-space extent along the view direction that projects to near zero pixels regardless of
	// how wide the frame is in world units, and the border - the only thing left to see once the
	// fill vanishes - disappears with it (task 33: "jak patrzy sie na niego z boku to go w ogole
	// nie widac"). Falls back to the plain world-space width for whichever axis has no valid
	// projection for `plane.center` (e.g. it is behind the camera).
	[[nodiscard]] float ScenePlaneBorderWidth(
		const RendererWindowState::ScenePlane &plane, const RendererViewCamera &camera,
		const glm::vec2 &viewportPixelSize);

	// Re-fits every anchored plane's centre, normal, tangent and extents from its atoms, once per
	// frame, next to ResolveAnchoredOrbitals. Planes with fewer than two resolvable anchors are
	// left exactly as they are - including their anchor list, which is the user's to clear.
	// The viewer-facing tie-break FitScenePlane needs for a collinear set comes from the window's
	// camera when it has one.
	void ResolveAnchoredScenePlanes(RendererWindowState &windowState);
} // namespace DefectStudio
