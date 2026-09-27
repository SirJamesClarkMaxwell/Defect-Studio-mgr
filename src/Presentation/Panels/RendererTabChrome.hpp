#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

namespace DefectStudio
{
	class EventBus;
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
	// Neither of those two functions changes here.

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


	// Draws the horizontal and the vertical toolbar as borderless overlays inside the rectangle the
	// active tab's viewport image occupies.
	//
	// `viewportOrigin` and `viewportSize` are that image's screen rectangle, which RendererPanel
	// already computes for the active window while it draws it. Passing the rectangle in rather
	// than deriving it here is deliberate: it is the same rectangle for a docked tab and for a
	// renderer window floating on its own, so neither case needs its own branch.
	//
	// Call this AFTER the loop that draws the renderer windows, never inside it - an overlay is a
	// window of its own, and the loop holds a reference into RendererLayer::GetWindows() that a
	// nested draw has no reason to be anywhere near.
	//
	// Draws nothing when `viewportSize` has no area, which is the no-active-tab case.
	void DrawViewportToolbarOverlays(
		RendererWindowState &windowState,
		RendererLayer &layer,
		ImVec2 viewportOrigin,
		ImVec2 viewportSize);

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
