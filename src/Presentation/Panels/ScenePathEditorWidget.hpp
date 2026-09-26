#pragma once

#include <string>
#include <vector>

#include "Renderer/Path/PathStyle.hpp"
#include "Renderer/Path/PathTypes.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// The stroke fields the Properties panel edits, read off one path and written back to every
	// selected path. A separate struct rather than a ScenePath copy because a multi-selection has
	// no single path to copy: the panel shows the first selected path's values and applies whatever
	// the user changed to all of them.
	//
	// Kept to what SetScenePathStyle can write. Node positions, segment kinds and bindings are not
	// style, do not go through this widget, and belong to S12 and S13.
	struct ScenePathStyleEdit
	{
		StrokeProfile profile = StrokeProfile::Round;
		glm::vec3 ribbonNormal{0.0f, 1.0f, 0.0f};
		float width = 0.05f;
		float alpha = 1.0f;
		glm::vec3 color{0.95f, 0.35f, 0.1f};
		PathDecorationKind startDecoration = PathDecorationKind::None;
		PathDecorationKind endDecoration = PathDecorationKind::None;
		PathDepthMode depthMode = PathDepthMode::DepthTest;
	};

	// The values the editor opens with for `selection`: the first selected path's style, or the
	// defaults when the selection is empty or resolves to nothing. `mixed` names the fields whose
	// value differs across the selection, so the panel can show them as indeterminate instead of
	// silently presenting the first path's value as if it were everyone's.
	struct ScenePathStyleEditState
	{
		ScenePathStyleEdit values;
		bool mixedProfile = false;
		bool mixedRibbonNormal = false;
		bool anyFlatProfile = false;
		bool mixedWidth = false;
		bool mixedAlpha = false;
		bool mixedColor = false;
		bool mixedStartDecoration = false;
		bool mixedEndDecoration = false;
		bool mixedDepthMode = false;
		// How many of `selection` actually resolved to a live path. Zero means there is nothing to
		// draw an editor for, whatever the selection vector says.
		std::size_t resolved = 0;
	};

	// Pure: no ImGui, no window mutation. This is the half of the editor that is testable.
	[[nodiscard]] ScenePathStyleEditState ResolveScenePathStyleEdit(
		const RendererWindowState &windowState, const std::vector<SceneObjectId> &selection);

	// Writes `edit` onto every selected path through SetScenePathStyle, so one undo entry covers
	// the whole multi-selection and the store's style revision is bumped for each. Returns the
	// number of paths actually changed.
	//
	// Applies every field. The panel calls this only when a control reported a change, and a field
	// the user did not touch still holds the value the editor opened with - which, for a mixed
	// field, is the first path's. That is the same "editing a mixed field flattens it" behaviour
	// every other multi-selection editor in this panel has.
	std::size_t ApplyScenePathStyleEdit(
		RendererWindowState &windowState, const std::vector<SceneObjectId> &selection, const ScenePathStyleEdit &edit);

	// Renames one path. Separate from the style edit because a name is per-object by definition:
	// there is no sensible multi-selection meaning for it.
	//
	//   ponytail: this DOES bump the store's style revision and so costs one re-mesh, because
	//   MutateStyle is the only mutable access PathStore offers and a name is not style. An ImGui
	//   text field fires per keystroke, so renaming a path re-meshes it once per character. That is
	//   nothing for the handful of paths a window holds, and the alternative - a third revision
	//   counter, or a MutateMetadata the mesher is trusted to ignore - is a change to the store's
	//   whole invalidation contract for a field nothing downstream reads. Upgrade path if a window
	//   ever holds hundreds of paths: add PathStore::MutateMetadata bumping neither counter, and
	//   move persistKey onto it too (SceneObjectPersistence.cpp:86 has the same problem).
	bool RenameScenePath(RendererWindowState &windowState, SceneObjectId id, std::string name);

	// The label a row or a section header shows for one path: its `name` when it has one, else
	// "Path #<index>" with the store index, so an unnamed path is still distinguishable.
	[[nodiscard]] std::string ScenePathDisplayName(const ScenePath &path, std::size_t storeIndex);

	// The ImGui half: draws the editor for the current selection and applies what changed.
	// Defined in ScenePathEditorWidget.cpp; returns true when it wrote anything.
	bool DrawScenePathEditor(RendererWindowState &windowState);
} // namespace DefectStudio
