#pragma once

#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathTypes.hpp"

namespace DefectStudio
{
	// Screen-space markers for a path's directly editable elements: one per node, plus the two bezier
	// handles of every Cubic segment. This is the single source both Edit Mode's overlay (S12) and
	// picking (PathPicking) read, so a handle can never be drawn where it cannot be clicked - the bug
	// task/40 spent a manual round chasing on scene arrows.
	//
	// Nothing here touches ImGui or the camera class: a view-projection matrix and a viewport size are
	// the whole input, which is what keeps this in Renderer and testable without a GL context.

	// Draw radii are the dot the user sees; pick radii are deliberately much larger. The numbers come
	// from task/40's live testing on scene arrow handles (ViewportGizmo.cpp:26-34), where real clicks
	// aimed at a handle landed tens of pixels away and an 11px radius missed nearly all of them.
	inline constexpr float kPathHandleDrawRadius = 5.0f;
	inline constexpr float kPathActiveHandleDrawRadius = 7.0f;
	inline constexpr float kPathHandlePickRadius = 20.0f;
	inline constexpr float kPathActiveHandlePickRadius = 26.0f;

	enum class PathMarkerKind
	{
		Node,
		BezierHandle,
	};

	struct PathHandleMarker
	{
		PathMarkerKind kind = PathMarkerKind::Node;
		PathElementId element;    // the node's or handle's own id
		PathElementId owner;      // BezierHandle: the node it is tethered to. Node: its own id.
		glm::vec3 worldPosition{0.0f};
		glm::vec2 screenPosition{0.0f}; // viewport pixels, origin top-left (SelectionHitTest convention)
		float drawRadius = kPathHandleDrawRadius;
		float pickRadius = kPathHandlePickRadius;
	};

	// Contract:
	// - One marker per node, in node order, then the handles of each Cubic segment in segment order
	//   (start handle before end handle). A Line or Arc segment contributes no handles.
	// - A node marker uses `resolved.positions[i]` when the index is in range, so a bound node's marker
	//   sits where the path is actually drawn rather than on its authored fallback.
	// - An element that projects behind the camera is OMITTED, not returned invisible: something with
	//   no position on screen has no hitbox and nothing to draw (plan S10, "hidden geometry has no
	//   hitbox"). Callers iterate what they get.
	// - `activeElement` gets the enlarged draw and pick radii; an unset or unmatched id enlarges
	//   nothing.
	// - A non-finite world position or a degenerate viewport yields no markers, never a NaN screen
	//   position.
	[[nodiscard]] std::vector<PathHandleMarker> BuildPathHandleMarkers(
		const ScenePath &path,
		const ResolvedNodes &resolved,
		const glm::mat4 &viewProjection,
		glm::vec2 viewportSize,
		PathElementId activeElement);

	// The world half-width of a stroke, in viewport pixels at `worldPosition`.
	//
	// `cameraRight` is a unit world vector perpendicular to the view direction. It is a parameter
	// rather than something derived here because a view-projection matrix alone does not hand one back
	// without an inverse, and every caller already holds the camera.
	//
	// Returns nullopt when the point projects behind the camera, the input is non-finite, or the
	// viewport is degenerate. Never negative, never NaN.
	[[nodiscard]] std::optional<float> ProjectWorldRadiusToPixels(
		const glm::mat4 &viewProjection,
		glm::vec2 viewportSize,
		const glm::vec3 &worldPosition,
		const glm::vec3 &cameraRight,
		float worldRadius);
}
