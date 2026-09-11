#pragma once

#include <imgui.h>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class RendererLayer;
	struct RendererWindowState;

	// The per-frame mouse pipeline every viewport shares, in the one order it works in. Both the
	// main renderer windows and the three-pane creation window call these: every time a piece of it
	// stayed behind as a RendererPanel member, the creation panes silently lost that feature, which
	// is how selection, the gizmo, the keybindings and box/circle select each broke in turn.

	// Label transforms, the keyboard-only pin shortcuts, and the short-circuiting gizmo/interaction
	// chain. Returns true while a gizmo or a label/arrow drag owns the frame's mouse - the caller
	// must then suppress its own picking and camera navigation.
	//
	// The `||` short-circuit is the point: once one handler claims the click, none of the ones
	// behind it may reinterpret the same click through its own hit-test.
	[[nodiscard]] bool RunViewportGizmoChain(
		RendererWindowState &windowState,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize,
		bool hovered,
		RendererLayer &layer,
		const WeakRef<CommandRegistry> &commandRegistry);

	// Draws the active selection tool's overlay (the box-drag rectangle or the circle brush, plus
	// its scroll-wheel radius) and hands the frame's mouse to it. Returns true if the tool consumed
	// the mouse, so the caller knows not to treat the click as a plain pick.
	//
	// A no-op returning false for the tools a pane has no business running (Cursor3D, the measure
	// tools) - those stay with RendererPanel, which owns the popups and dialogs they open.
	[[nodiscard]] bool DrawAndDispatchSelectionTools(
		RendererWindowState &windowState,
		const ImVec2 &imageOrigin,
		bool hovered,
		bool gizmoCapturing,
		RendererLayer &layer);
} // namespace DefectStudio
