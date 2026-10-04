#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "Core/Utils/Memory.hpp"

#include <imgui.h>

namespace DefectStudio
{
	class EventBus;
	class CommandRegistry;
	class RendererLayer;
	struct RendererWindowState;

	// Chrome that belongs to the renderer as a whole rather than to one renderer window.
	//
	// Renderer windows already read as tabs: RendererPanel docks each of them into the dockspace's
	// central node, and a dock node with several windows in it draws a tab bar. What they did not
	// have is chrome at that level. Both viewport toolbars were drawn inside every window by
	// RendererPanel::renderStructureWindow, so N tabs meant N copies of the same controls, each one
	// only reachable by first bringing its tab to the front - and the tab bar itself had nowhere to
	// put a "+".
	//
	// This file is the outer level: one pair of toolbars over whichever tab is showing, a "+" on the
	// tab bar, and Ctrl+W's end of the close path.
	//
	// StructureCreationTabsPanel already draws ONE pair of toolbars acting on its active pane. That
	// is the same shape, at a different scale, and it is why DrawViewportToolbar and
	// DrawViewportVerticalToolbar are free functions over a RendererWindowState in the first place.
	// The vertical toolbar receives the same command registry as its viewport.

	// The renderer window whose tab is currently showing, or an empty string when none is.
	//
	// Resolved from the central dock node's visible window, not from focus: the node's visible
	// window IS the front tab, which is the thing the user is looking at, whereas focus can sit in
	// the outliner while a renderer tab is on screen. Falls back to
	// RendererLayer::GetLastFocusedViewportWindowId() when the central node has no renderer window
	// in it, which is what keeps a renderer window the user dragged out of the dockspace working.
	//
	// The id is recovered from the ImGui window name, which RendererPanel builds as
	// "<title>###RendererWindow_<windowId>" - that "###" suffix is already the stable identity
	// ImGui keys docking and focus on, so reading it back is not a second naming scheme.
	[[nodiscard]] std::string ResolveActiveRendererWindowId(RendererLayer &layer);

	// The windowId inside an ImGui window name of the form "<title>###RendererWindow_<windowId>",
	// or an empty string when the name is not one of ours.
	//
	// Split out of ResolveActiveRendererWindowId because it is the part that can be wrong in a way
	// nothing on screen reveals - a title containing "###" of its own, a name with the marker but no
	// id after it - and because it is the only part of this file a test can reach without an ImGui
	// frame and a live dockspace.
	[[nodiscard]] std::string ParseRendererWindowId(std::string_view imguiWindowName);


	// Draws the horizontal and the vertical toolbar over the active tab's viewport image.
	//
	// They are CHILD windows of the renderer window, submitted from inside it, and that is the whole
	// point. The first version made them top-level ImGui windows drawn after the loop over the
	// renderer windows. A top-level window is always above everything drawn inside another window,
	// so the toolbars covered the N side panel and clipped the navigation gizmo - both of which live
	// in the renderer window's own draw list - and, not being clipped by it, they spilled across
	// neighbouring docked panels. Children are clipped to the parent and take their z-order from the
	// order they are submitted in, which is what this needs.
	//
	// Submit this right after the viewport image and before everything drawn on top of it. The side
	// panel goes last on purpose ("so the N panel floats over everything already drawn on the
	// image") and must keep winning over these.
	//
	// `viewportOrigin` and `viewportSize` are the image's screen rectangle, already to hand at that
	// point. Returns the measured horizontal overlay height plus its style-derived clearance margin,
	// or zero when the rectangle has no area.
	[[nodiscard]] float DrawViewportToolbarOverlays(
		RendererWindowState &windowState,
		RendererLayer &layer,
		ImVec2 viewportOrigin,
		ImVec2 viewportSize,
		const WeakRef<CommandRegistry> &commandRegistry);

	// Appends a "+" to the central dock node's tab bar and publishes
	// RendererEvents::Windows::OpenEmptyRequested when it is clicked - the same event Ctrl+T, the
	// main menu and the project tree publish. Three ways in, one action.
	//
	// Does nothing when the central node holds no renderer window: a "+" on a tab bar full of
	// unrelated panels would be a new-tab button for something that is not there.
	void DrawRendererTabBarAddButton(RendererLayer &layer, EventBus &eventBus);

	// True when closing this window would discard something the user put in it: a structure, or any
	// scene object - paths, arrows, orbitals, planes, free labels, pinned measurements.
	//
	// This is the only question Ctrl+W's confirmation asks. It deliberately does NOT ask whether
	// the content is saved: a free window's scene objects have nowhere to be saved to yet (that is
	// the owner-key half of the renderer-tabs plan, not this task), so "unsaved" would be true for
	// all of them and the prompt would carry no information.
	[[nodiscard]] bool RendererTabHoldsContent(const RendererWindowState &windowState);

	// Ctrl+W's end of the close path.
	//
	// Closing is deferred rather than immediate because the event arrives at an arbitrary point in
	// the frame and RendererPanel iterates GetWindows() by reference - removing a window from under
	// that loop invalidates it. RendererPanel already drains a deferred close list for the window's
	// own X button; this feeds the same list.
	//
	// An empty tab closes with no prompt, exactly as clicking X does today. A tab that holds
	// content asks first, because Ctrl+W sits one key away from Ctrl+E, Ctrl+S and Ctrl+Q and what
	// it discards does not come back.
	class RendererTabCloseCoordinator
	{
	public:
		// Called from the CloseRequested subscription. An empty `windowId` means the active tab and
		// is resolved in Drain, not here - which tab is active is a question about this frame, and
		// the event may have arrived during the previous one.
		//
		// A second request while a confirmation is already up is ignored: holding Ctrl+W must not
		// stack prompts.
		void Request(const std::string &windowId);

		// Resolves a pending request and appends to `windowsToClose` the ids cleared to close this
		// frame. Draws the confirmation modal while one is open.
		//
		// Confirming appends the id and clears the request; cancelling, closing the modal, or a
		// request naming a window that no longer exists clears it without appending.
		void Drain(RendererLayer &layer, std::vector<std::string> &windowsToClose);

	private:
		std::string m_PendingWindowId;
		bool m_Resolved = false;
		bool m_ConfirmOpen = false;
	};
} // namespace DefectStudio
