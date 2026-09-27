#pragma once

#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio
{
	// G / R / S over the elements selected inside one path, while Edit Mode is on.
	//
	// The mirror of SceneTransformPaths, one level down. That file composes a delta onto a whole
	// path's object transform; this one moves nodes and Bezier handles inside a path whose object
	// transform does not change at all.
	//
	// It deliberately reuses the whole modal machinery rather than growing a second one: the same
	// ModalTransformSession computes the delta, the same constraints and snapping apply, the same
	// numeric entry works, and the same Escape restores. Only the capture and the apply differ, which
	// is exactly the seam SceneTransformSelectionSnapshot already provides for five other object
	// kinds.

	// Fills `snapshot.pathElements` from the window's Edit Mode session and LEAVES `snapshot.paths`
	// EMPTY.
	//
	// That emptying is the point, not an omission. If both were filled, one G would move the object
	// transform and the node positions, and the node would travel twice. Edit Mode and Object Mode
	// are two levels of the same selection and exactly one of them owns a given drag.
	//
	// Captures nothing when the session is inactive, when its path is gone, or when its element
	// selection is empty.
	void CaptureSceneTransformPathElements(
		const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot);

	// Applies `delta` to the captured elements, always from the captured start values rather than
	// from the current ones, so a drag that passes back through its origin lands exactly where it
	// began.
	//
	// The one piece of real arithmetic here, and the place a bug will hide:
	//
	//   The modal delta is in WORLD space. A node's position and a handle's offset are in the path's
	//   LOCAL space. So a translation must be carried into local space through the inverse of the
	//   path transform's rotation and scale - NOT by adding the world delta to a local position,
	//   which is only correct under the identity transform and silently wrong the moment the user
	//   has moved, turned or scaled the path in Object Mode.
	//
	//   The same applies to the pivot for Rotate and Scale: it arrives in world space and the
	//   rotation happens among local positions.
	//
	// Element rules:
	//
	// - A NODE translates by the local delta. Its handles are stored as offsets from it
	//   (PathHandle::offset), so they follow with no arithmetic of their own - and must not be
	//   moved a second time even when they are in the selection alongside their node. A node in the
	//   selection wins over its own handles.
	// - A HANDLE translates by changing its offset. Its node does not move.
	// - A SEGMENT translates both of its end nodes, under the same rule: a node already moving
	//   because it is selected, or because another selected segment shares it, moves once.
	// - Rotate and Scale act on the same set of moving points, but NOT all about the same pivot:
	//
	//     a moving NODE turns and scales about the selection pivot, as every other object does;
	//     a moving HANDLE turns and scales about ITS OWN NODE, whatever the pivot mode says.
	//
	//   This is Blender's rule and it is the only one that means anything. A handle is stored as an
	//   offset from its node, so scaling it about its node is exactly "make this whisker longer or
	//   shorter" and rotating it is exactly "swing this whisker round" - the two things a person
	//   reaches for R and S to do. About the selection median instead, S on a lone handle would drag
	//   it bodily towards a point somewhere else on the curve, which is not an operation anyone
	//   wants and which the single-selection case degenerates to nothing anyway.
	//
	//   Translate is unaffected: a delta does not consult a pivot.
	//
	//   The modal's OWN pivot must agree with this. A selected handle therefore contributes its
	//   owner node's position to SceneTransformPivotPositions, not its own: the constraint line, the
	//   pivot the delta is measured against and the point the apply turns about are then one point.
	//   While they disagreed, the axis line stood on the handle and the rotation happened about the
	//   node, so the result never matched what was drawn.
	//
	//   The node in question is the handle's owner, at its position for this frame. When the owner is
	//   itself selected the handle is not transformed at all - see the rule above - so the two never
	//   have to agree about who moved first.
	//
	// After the positions change, re-derive the Auto handles with ApplyAutoHandles. Handles typed
	// Free, Aligned or Vector are left exactly as they are.
	//
	//   ponytail: dragging one half of an ALIGNED pair does not yet swing its twin to match. There
	//   is no rule in PathHandleRules that does it - MakeTangent and ApplyAutoHandles both work off
	//   neighbouring geometry, not off a twin - and inventing one inside a transform apply is how a
	//   second, disagreeing handle model gets born. Upgrade path: give PathHandleRules an
	//   EnforceAlignedTwin(path, handle) and call it from here, so the rule lives with the other
	//   handle rules and Edit Mode is only its caller.
	//
	// Goes through ApplyPathEdit with PathRevisionKind::Geometry, the same way
	// ApplySceneTransformPaths does, so the whole drag is one undo entry and the caches are
	// invalidated once.
	void ApplySceneTransformPathElements(
		RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, ModalTransformOp operation,
		TransformPivotMode pivotMode, const glm::vec3 &selectionPivot);

	// Puts every captured element back to its start value. Escape during a drag, and the undo of a
	// finished one, both land here.
	void RestoreSceneTransformPathElements(
		RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot);
} // namespace DefectStudio
