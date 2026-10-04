#pragma once

namespace DefectStudio
{
	class CommandRegistry;
	class ContextManager;
	class RendererLayer;

	// Keybinding context that is active while the focused viewport has a path open in Edit Mode.
	// The path_edit bindings in keybindings.yaml carry it and are listed after the viewport-wide
	// bindings on the same keys (E roll, V view cycle, Delete selection, 1/2/3 axis align), so the
	// resolver's "later registration wins" rule hands those keys to Edit Mode only while it is open.
	inline constexpr const char *kPathEditActiveContext = "renderer.path_edit.active";

	// Every Path Edit Mode action as a registered command (v2 plan C12) instead of raw ImGui key
	// polling. Each command acts on the focused viewport window - or on the only window when none
	// is focused, same rule as renderer.scene_arrow.reverse - and does nothing, successfully, when
	// that window has no path open in Edit Mode (toggle excepted, see below).
	//
	//   renderer.path_edit.toggle            Tab     enter on the single selected path / leave
	//   renderer.path_edit.leave             Escape
	//   renderer.path_edit.mode_nodes        1
	//   renderer.path_edit.mode_segments     2
	//   renderer.path_edit.mode_whole        3
	//   renderer.path_edit.extend            E       ExtendSelectedScenePathEnd
	//   renderer.path_edit.insert            -       InsertSelectedScenePathSegment
	//   renderer.path_edit.delete_nodes      Delete  DeleteSelectedScenePathNodes
	//   renderer.path_edit.reverse           -       ReverseEditedScenePath
	//   renderer.path_edit.handle_type_menu  V       asks the viewport to open the handle-type popup
	//   renderer.path_edit.handle_free       -       SetSelectedScenePathHandleType(Free)
	//   renderer.path_edit.handle_aligned    -       ... Aligned
	//   renderer.path_edit.handle_vector     -       ... Vector
	//   renderer.path_edit.handle_auto       -       ... Auto
	//
	// toggle with no path open enters Edit Mode only when exactly one existing path is selected,
	// otherwise it is a successful no-op. An edit operation that the path layer rejects (wrong
	// element selected, invalid topology) returns that StructuredError unchanged.
	//
	// While the window has a modal G/R/S in progress (`modalTransform` engaged) every one of these
	// commands is a successful no-op: the modal loop owns Escape, digits and axis keys until it ends,
	// and Escape must cancel the drag, not also leave Edit Mode underneath it.
	void RegisterScenePathEditCommands(CommandRegistry &registry, RendererLayer &rendererLayer);
	// Object Mode reverse plus the existing Alt+R command id as a compatibility alias.
	void RegisterScenePathObjectCommands(CommandRegistry &registry, RendererLayer &rendererLayer);

	// Sets kPathEditActiveContext from the focused viewport window's PathEditSession (false when no
	// viewport is focused). Called once per frame by the renderer panel.
	void UpdateScenePathEditContext(const RendererLayer &rendererLayer, ContextManager &contextManager);
} // namespace DefectStudio
