#pragma once

#include <string>
#include <unordered_map>

#include <imgui.h>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class ContextManager;
	class RendererLayer;
	struct RendererWindowState;

	// Publishes FocusChanged when this viewport gains or loses focus, and re-derives the
	// renderer.viewport.focused keybinding context from RendererLayer afterwards.
	//
	// Every viewport must call this, not just the ordinary renderer windows: the context gates every
	// viewport keybinding, and findViewportCommandWindow resolves a command target from the focused
	// window id it maintains. A viewport that never reports focus has no working shortcuts at all -
	// which is what the three-pane creation window had before it called this.
	void UpdateViewportFocusState(
		RendererWindowState &windowState,
		bool nowFocused,
		RendererLayer &layer,
		const WeakRef<ContextManager> &contextManager);

	// Mouse orbit/pan/zoom for one viewport. A free function because the three-pane creation
	// window needs exactly this behaviour on whichever pane is hovered and active, and it only
	// ever touched the layer plus the per-window last-mouse-position map the caller owns.
	// Call it only when the viewport is hovered - it reads the global mouse state unconditionally.
	void ApplyViewportInputNavigation(
		RendererWindowState &windowState,
		float deltaTime,
		std::unordered_map<std::string, ImVec2> &lastMousePositions,
		RendererLayer &layer);

	// Held-key atom nudge and camera pan. Polled per frame rather than driven by keybindings, so -
	// unlike every `repeatable: true` binding - they only run for viewports whose panel calls them.
	// Both are free functions for the same reason the two above are: the creation panes need them,
	// and a pane is not a RendererPanel window.
	void ApplyContinuousKeyboardNudge(
		RendererWindowState &windowState,
		float deltaTime,
		RendererLayer &layer,
		const WeakRef<CommandRegistry> &commandRegistry);

	// Completes the interaction opened by repeatable orbit/roll/zoom commands once their chord is
	// released. Called before continuous pan so a changed chord can start a fresh interaction.
	void CommitFinishedKeyboardViewInteraction(RendererWindowState &windowState, RendererLayer &layer);

	void ApplyContinuousKeyboardPan(RendererWindowState &windowState, float deltaTime, RendererLayer &layer);
} // namespace DefectStudio
