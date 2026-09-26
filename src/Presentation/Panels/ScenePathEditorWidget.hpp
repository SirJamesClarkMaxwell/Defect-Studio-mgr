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
		// Flat only, and shown only for a Flat selection: zero is the sheet, positive extrudes it.
		float ribbonThickness = 0.0f;
		float width = 0.05f;
		float alpha = 1.0f;
		glm::vec3 color{0.95f, 0.35f, 0.1f};
		// task/41 S11n. Both have been in the model, in the file format and in the mesher since S5,
		// and neither had a single control - the same way the decoration scales were unreachable
		// until S11i. Editing them is the whole of this slice; no new rendering is needed.
		//
		// `gradient` overrides `color` along the stroke when it is enabled and has stops. The panel
		// edits the stop list directly: position in [0,1] of arc length, colour and alpha per stop.
		PathDashStyle dash;
		PathGradient gradient;
		// task/41 S11i: the whole decoration, not just its kind. The two scales and `filled` are the
		// shape control the panel was missing - a tip's proportions were editable in the file format
		// and in code, but nowhere in the UI, so every tip in the app was stuck at 1.0/1.0/solid.
		PathEndpointDecoration startDecoration;
		PathEndpointDecoration endDecoration;
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
		bool mixedRibbonThickness = false;
		bool anyFlatProfile = false;
		bool mixedWidth = false;
		bool mixedAlpha = false;
		bool mixedColor = false;
		// True when any field of the dash pattern, or any stop of the gradient, differs across the
		// selection. One flag each: the panel draws them as one group, and a selection whose stops
		// merely differ in count has no single gradient to show.
		bool mixedDash = false;
		bool mixedGradient = false;
		// True when ANY field of that endpoint's decoration differs across the selection - kind,
		// either scale or `filled`. One flag per endpoint rather than four: the panel draws the
		// endpoint as one group, and a selection where only the scales differ is still a selection
		// with no single decoration to show.
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
	// `recordUndo == false` applies the edit through the silent context instead, leaving undo
	// history untouched. That is what a drag in progress uses: see BeginScenePathStyleDrag below.
	std::size_t ApplyScenePathStyleEdit(
		RendererWindowState &windowState, const std::vector<SceneObjectId> &selection,
		const ScenePathStyleEdit &edit, bool recordUndo = true);

	// The undo boundary for a held widget. ImGui reports a change on every frame a DragFloat,
	// SliderFloat or ColorEdit is held, and applying each of those with recordUndo == true is what
	// made one drag of the Width slider cost one Ctrl+Z per frame.
	//
	// Contract, and the reason these are exposed rather than kept inside DrawScenePathEditor: they
	// are the testable half. A test cannot drive ImGui, but it can call Begin, apply N times
	// silently, call Commit, and assert the stack grew by exactly one entry that restores the
	// pre-Begin state.
	//
	// - Begin captures `windowState.scenePathStyleEditBefore` from the CURRENT state, i.e. before
	//   the first silent apply. Calling it again while a drag is open is a no-op, so the panel may
	//   call it unconditionally on activation without tracking which widget is live.
	// - Commit pushes exactly one undo entry holding that snapshot and clears it. It is a no-op
	//   returning false when no drag is open, and also when nothing actually changed between Begin
	//   and Commit - a click that activates a slider without moving it leaves no history.
	//   "Nothing changed" means no path's revision moved, compared against the snapshot's own store.
	//   PathStore bumps a revision on every mutation and the snapshot carries a whole store, so this
	//   costs a counter comparison. Do NOT give PathStore an equality operator for this: a drag that
	//   wanders away and returns to its starting value does record an entry, and that is fine.
	// - An abandoned drag (selection cleared, window closed, panel hidden mid-drag) leaves the
	//   snapshot behind. Commit is therefore safe to call on a selection that no longer matches the
	//   one Begin saw: the snapshot restores whole-window scene objects, not a selection.
	void BeginScenePathStyleDrag(RendererWindowState &windowState);
	bool CommitScenePathStyleDrag(RendererWindowState &windowState);

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

	// The line-style presets the panel offers, mapped onto `PathDashStyle`'s two lengths. Named
	// rather than left as two raw numbers because "dotted" is a thing users ask for and
	// "dashLength 0.02, gapLength 0.02" is not.
	//
	//   ponytail: Solid, Dashed and Dotted are all `PathDashStyle` can express - it is one dash and
	//   one gap repeated. Dash-dot and dash-dot-dot need a pattern of alternating lengths, which is a
	//   change to the style, the format and PathDash's arc-length walk. Deliberately not done here;
	//   the upgrade path is a `std::vector<float>` pattern with the current two lengths as its
	//   two-element case.
	enum class ScenePathLineStyle
	{
		Solid,
		Dashed,
		Dotted,
		Custom, // the numbers were edited by hand and match no preset
	};

	// Which preset `dash` corresponds to, or Custom. Pure.
	[[nodiscard]] ScenePathLineStyle ResolveScenePathLineStyle(const PathDashStyle &dash);

	// Writes the preset's lengths into `dash`, scaled to `strokeWidth` so a dotted line of a thick
	// stroke has proportionally longer gaps. Custom leaves `dash` alone. Pure.
	void ApplyScenePathLineStyle(PathDashStyle &dash, ScenePathLineStyle style, float strokeWidth);

	// The ImGui half: draws the editor for the current selection and applies what changed.
	// Defined in ScenePathEditorWidget.cpp; returns true when it wrote anything.
	bool DrawScenePathEditor(RendererWindowState &windowState);
} // namespace DefectStudio
