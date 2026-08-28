#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// Default SceneArrow anchored at seedPosition (typically the 3D cursor if placed, else world
	// origin) - shared by every "Add Arrow" entry point (ObjectPropertiesPanel's button, Shift+A
	// menu, right-click Add submenu) so they all seed a visible, non-zero-length arrow the same way.
	// Fallback overload with no scene context - fixed 1.0 length, used only where a
	// RendererWindowState genuinely isn't available.
	[[nodiscard]] RendererWindowState::SceneArrow MakeDefaultSceneArrow(const glm::vec3 &seedPosition);

	// Scene-relative overload (docs/scene_arrow_rework_plan_corrected.md Step 5) - length scales
	// with the structure's bounding diagonal so a freshly-added arrow already looks reasonable
	// instead of needing a manual resize on every add. All three "Add Arrow" entry points use this
	// one; the seed-only overload above is the fallback for call sites without a window.
	[[nodiscard]] RendererWindowState::SceneArrow MakeDefaultSceneArrow(
		const RendererWindowState &windowState, const glm::vec3 &seedPosition);

	// Full = properties panel (every section expanded/collapsible). Compact = Blender-style
	// "adjust last operation" quick-edit popup (Geometry/Appearance always visible, Placement/
	// Advanced collapsed). Same underlying controls either way - see DrawSceneArrowEditor.
	enum class SceneArrowEditorMode
	{
		Full,
		Compact
	};

	// Start/End position, kind/orientation/plane combos, and the full ArrowStyle editor for one
	// SceneArrow - shared by ObjectPropertiesPanel's "Arrows" list and RendererPanel's
	// quick-edit-on-add popup so the two don't duplicate this UI. Defined in ObjectPropertiesPanel.cpp
	// (where the widgets originated); does not draw a remove/select row of its own - callers wrap
	// this with whatever row chrome fits their own context (list row with an X button, or a bare
	// popup with none). No-undo: mutates the struct directly, for callers that don't have a
	// RendererWindowState (or genuinely don't want the edit to be undoable).
	void DrawSceneArrowEditor(RendererWindowState::SceneArrow &arrow);

	// Undoable, mode-aware variant - validates arrowIndex, pushes one undo snapshot per logical
	// edit (activation-based, not per-frame - see the .cpp), and lays out Full vs Compact sections.
	// ObjectPropertiesPanel and the quick-edit popup should prefer this one over the bare-reference
	// overload above.
	// globalSettings supplies ApplySceneArrowKindChange's default Arrow3D/Line proportions (Settings >
	// Renderer > Scene arrows) - defaults to the struct's own shipped ratios for callers that don't
	// have a RendererLayer at hand.
	void DrawSceneArrowEditor(
		RendererWindowState &windowState,
		std::size_t arrowIndex,
		SceneArrowEditorMode mode,
		const RendererGlobalRenderSettings &globalSettings = {});

	// Arrow2D's geometry fields are screen-space pixels; Line/Arrow3D's are world-space full
	// diameters - the same numbers can never be reinterpreted across that boundary. Switching
	// ArrowKind must go through this helper (never a raw `arrow.kind = newKind`), which applies the
	// target kind's own geometry defaults while preserving start/end/color/alpha/outlineColor and
	// the remembered orientation2D/fixedPlane.
	void ApplySceneArrowKindChange(
		RendererWindowState::SceneArrow &arrow,
		RendererWindowState::ArrowKind newKind,
		const RendererGlobalRenderSettings &globalSettings = {});

	// Shared by RendererPanel's viewport Delete and ObjectPropertiesPanel's "X" button so the two
	// don't duplicate (and drift on) the same erase/index-normalization logic: sorts descending,
	// de-duplicates, ignores out-of-range indices, clears selection/drag state, and closes the
	// quick-edit popup if anything was actually erased (simpler and just as safe as tracking whether
	// the specific erased index matched the quick-edit target, since a cleared selection already
	// fails the quick-edit panel's own "exactly one, matching" guard either way). Caller is
	// responsible for pushing the undo snapshot first.
	void EraseSceneArrows(RendererWindowState &windowState, std::vector<std::size_t> indices);

	// In-process whole-arrow clipboard for Ctrl+C/Ctrl+V/Ctrl+D on SceneArrow selections (RendererPanel's
	// raw key checks, mirroring the Delete-key precedent - see RendererAtomEditCommands.cpp's
	// GetAtomClipboard for the equivalent atom-side pattern). Shared across every window/instance, not
	// persisted. Copy/Duplicate/Paste all push their own undo snapshot before mutating.
	[[nodiscard]] std::vector<RendererWindowState::SceneArrow> &GetSceneArrowClipboard();
	void CopySceneArrowsToClipboard(const RendererWindowState &windowState);
	void DuplicateSelectedSceneArrows(RendererWindowState &windowState);
	void PasteSceneArrowsFromClipboard(RendererWindowState &windowState);

	// Geometry (shaftWidth/headWidth/headLength/outlineWidth) and Style (color/alpha/outlineColor)
	// clipboards for the viewport context menu's "Copy/Paste Geometry|Style|Geometry+Style" actions -
	// two independent optional slots rather than one tagged slot, so e.g. Paste Geometry stays disabled
	// until Geometry (or Geometry+Style) has actually been copied, instead of pasting a stale/irrelevant
	// value from a Style-only copy.
	[[nodiscard]] std::optional<RendererWindowState::ArrowStyle> &GetArrowGeometryClipboard();
	[[nodiscard]] std::optional<RendererWindowState::ArrowStyle> &GetArrowStyleClipboard();
	void CopyArrowGeometry(const RendererWindowState::ArrowStyle &style);
	void CopyArrowStyle(const RendererWindowState::ArrowStyle &style);
	// Applies to every arrow index in `targets`; no-op (returns false, nothing mutated) if the relevant
	// clipboard is empty. Like EraseSceneArrows, the caller pushes the undo snapshot first - so a
	// combined "Paste Geometry + Style" call site can push exactly one snapshot covering both instead of
	// two (one per function) that would need two Undos to fully revert.
	bool PasteArrowGeometry(RendererWindowState &windowState, const std::vector<std::size_t> &targets);
	bool PasteArrowStyle(RendererWindowState &windowState, const std::vector<std::size_t> &targets);
} // namespace DefectStudio
