#pragma once

#include <glm/glm.hpp>

#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathTessellator.hpp"
#include "Renderer/Path/PathTypes.hpp"

namespace DefectStudio
{
	// Hit-testing one path against a viewport cursor. The whole point of this file is that it consumes
	// the SAME EvaluatedPath the renderer meshed and the SAME decoration contours the mesher built -
	// picking that re-derives its own geometry drifts from what is on screen the first time either
	// side changes (plan v2 C7).
	//
	// Pure: no camera class, no window state, no GL, no selection. What the caller does with a hit -
	// which path wins between two overlapping ones, what gets selected - is the caller's arbitration.

	// Extra pixels around the drawn stroke that still count as a hit. Anti-aliasing plus a thin tube
	// seen from a distance otherwise makes a visually solid line nearly unclickable.
	inline constexpr float kPathStrokePickTolerance = 4.0f;

	enum class PathPickKind
	{
		None,
		Handle,     // a bezier handle of a Cubic segment
		Node,
		Decoration, // an endpoint decoration; `element` is the endpoint NODE it sits on
		Segment,
		WholePath,  // Object Mode: the stroke or a decoration was hit, the element is not the point
	};

	struct PathPickResult
	{
		PathPickKind kind = PathPickKind::None;
		SceneObjectId path;
		PathElementId element;          // unset for None and WholePath
		glm::vec3 worldPosition{0.0f};  // the point on the path nearest the cursor; caller's depth input
		float screenDistance = 0.0f;    // pixels from the cursor to the hit geometry; 0 when inside it

		[[nodiscard]] bool Hit() const
		{
			return kind != PathPickKind::None;
		}
	};

	struct PathPickSettings
	{
		glm::mat4 viewProjection{1.0f};
		glm::vec2 viewportSize{0.0f};
		glm::vec2 cursor{0.0f};      // viewport pixels, origin top-left, relative to the viewport image
		glm::vec3 cameraRight{1.0f, 0.0f, 0.0f}; // unit, perpendicular to the view direction
		PathElementId activeElement; // gets the enlarged handle pick radius
		// Object Mode is the default: nodes and handles are not drawn, so they are not pickable, and
		// any hit collapses to WholePath. Edit Mode (S12) turns this on and gets element-level hits.
		bool editMode = false;
	};

	// Arbitration order, highest first: Handle > Node > Decoration > Segment. The first candidate
	// within its tolerance wins outright - a node sitting on top of the stroke is never "the stroke,
	// because it was 2px closer".
	//
	// Contract:
	// - `path.visible == false` or `path.renderable == false` returns None. Hidden geometry has no
	//   hitbox, and that includes its handles.
	// - Handle and node hits use the PathHandleGeometry markers and radii, so what is pickable is
	//   exactly what S12 draws. An element that projects behind the camera has no marker and no hit.
	// - The stroke is tested as a screen-space polyline over `evaluated.samples`, with the per-sample
	//   projected half-width plus kPathStrokePickTolerance. Samples behind the camera are skipped
	//   along with the polyline edges touching them.
	//   ponytail: a Flat ribbon seen edge-on picks as wide as a tube of the same width, because the
	//   half-width is projected along cameraRight rather than along the ribbon's own normal. Upgrade
	//   path when someone notices: project the sample's `normal` for StrokeProfile::Flat.
	// - A decoration is tested as a capsule from the endpoint back along the path to its `trim`
	//   distance, at the contour's widest half-width. A None decoration, or one whose contour is
	//   empty, is not a candidate.
	// - The dash pattern is NOT consulted: a dashed stroke picks along its whole span, gaps included.
	//   Clicking a gap and having the line not respond is worse than the alternative, and S13's
	//   per-element editing never needs the gap to be dead.
	// - `evaluated.samples.size() < 2` leaves only the handles, nodes and decorations pickable.
	// - Non-finite settings, a degenerate viewport or a non-finite sample yields None, never a NaN
	//   distance.
	[[nodiscard]] PathPickResult PickPath(
		const ScenePath &path, const ResolvedNodes &resolved, const EvaluatedPath &evaluated,
		const PathPickSettings &settings);
}
