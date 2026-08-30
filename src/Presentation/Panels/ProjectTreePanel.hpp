#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "IO/ProjectRootsIO.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class EventBus;

	// Filesystem tree over the currently registered project roots (local folders or mounted
	// drives - mount transparency means this panel doesn't know or care which). One panel, N
	// roots, each its own collapsible section (see T07.5.4 in docs/work/project/TODO.md) - roots
	// come from either an active project's manifest.yaml or, with none open, the ad-hoc
	// ProjectRootsIO fallback; either way EditorLayer owns that decision and pushes the result in
	// via SetRoots(). Scope cut: this panel itself knows nothing about projects/manifests.
	class ProjectTreePanel final : public IPanel
	{
	public:
		explicit ProjectTreePanel(
			Ref<EventBus> eventBus,
			std::string title = "Project Tree",
			bool visibleByDefault = true);
		ProjectTreePanel(const ProjectTreePanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Replaces the displayed root list wholesale - called by EditorLayer whenever the
		// authoritative list changes (add/remove/change-folder, project create/open). This panel
		// never mutates m_Roots itself; toolbar/context-menu actions only queue events and wait
		// for the next SetRoots() round-trip (one frame of lag, same tradeoff already made for
		// keyboard nav below).
		void SetRoots(std::vector<ProjectRootEntry> roots);

		// Arms picker mode (DisplacementComparisonPanel's in-app "Browse...", routed through
		// EditorLayer) - makes this panel visible; the next file the user confirms (Enter, or a
		// single click while picking) fires ProjectEvents::DisplacementComparisonFilePicked and
		// disarms picker mode. Esc cancels without firing anything.
		void RequestFilePick();

	private:
		void renderToolbar();
		void renderAddRootPopup();
		void renderRootSection(const ProjectRootEntry &section);
		void renderRootSectionContextMenu(const ProjectRootEntry &section);
		void renderDirectoryContents(const Path &directory);
		void renderDirectoryContextMenu(const Path &directory);
		void renderFileContextMenu(const Path &filePath);
		// Copy/Cut/Paste/Rename/Delete/New Folder/New File - shared by renderDirectoryContextMenu,
		// renderFileContextMenu, and renderRootSectionContextMenu. isContainer gates New Folder/New
		// File/Paste (true for a directory or a root); isRealEntry gates Copy/Cut/Rename/Delete
		// (true for a file or a subdirectory, false for a root - a root is a registered pointer to a
		// folder, not itself a filesystem entry to copy/rename/delete; see the .cpp for why).
		void renderFileOpsMenuItems(const Path &entryPath, bool isContainer, bool isRealEntry);
		// VSCode-style: Up/Down move selection, Right expands, Left collapses (or jumps to parent
		// if already collapsed/a file), Enter toggles a folder, Shift+Enter opens the selected
		// folder as a defect (same action as the RMB menu). Reads m_VisibleFlatList as rebuilt this
		// same frame by rebuildVisibleFlatList() (called before any of this at the top of Render()),
		// then updates m_SelectedPath/m_ExpandedPaths - the tree render right after picks those up
		// via SetNextItemOpen. Both maps are keyed by absolute filesystem path, already globally
		// unique across every section, so none of this needs to be per-section.
		void handleKeyboardNavigation();
		// Walks m_Roots + m_ExpandedPaths (no ImGui calls - pure filesystem/state) to rebuild
		// m_VisibleFlatList fresh, synchronously, before anything else touches it this frame. Doing
		// this upfront (rather than accumulating it as a side effect of rendering, the previous
		// design) is what lets handleEntryClicked resolve a Shift-click range immediately instead of
		// deferring to a "next frame" pass - the deferred version was unreliable in practice (2026-08-29
		// bug report: Shift-click stopped working after freeze fixes, Ctrl-click kept working since it
		// never depended on this list).
		void rebuildVisibleFlatList();
		void collectVisibleEntries(const Path &directory);
		// Ctrl+C/X/V, F2 (rename), Del - mirrors handleKeyboardNavigation's focus gate and shares
		// its "acts on last frame's selection" timing (no per-frame lag issue here since these
		// don't need m_VisibleFlatList).
		void handleFileOpsKeyboardShortcuts();
		void openDefectAt(const Path &directory);
		void confirmFilePick(const Path &filePath);

		// Left-click/Ctrl-click/Shift-click selection - Shift-click's range resolves immediately
		// against m_VisibleFlatList (already fresh for this frame, see rebuildVisibleFlatList above).
		void handleEntryClicked(const std::string &pathKey);

		void renderCreateEntryPopup();
		void renderRenamePopup();
		void renderDeleteConfirmPopup();
		void renderPasteConflictPopup();
		// Renders one button of a modal's button row, wired into the shared m_ModalHighlightedButton
		// arrow-key cursor (Left/Right cycles it, Enter activates whichever button is highlighted) -
		// "classic" dialog keyboard nav without turning on Dear ImGui's app-wide Nav system
		// (io.ConfigFlags never sets NavEnableKeyboard; EditorLayer's camera shortcuts already use
		// bare arrow keys/WASD, and a global Nav flag would start contesting those - see
		// ImGuiLayer.cpp). Handles its own BeginDisabled/EndDisabled; returns true the frame this
		// button was activated, by mouse click or by Enter while highlighted (never true if disabled).
		bool modalButton(const char *label, int index, bool enabled = true);
		// Drains m_PasteQueue: performs every non-conflicting copy/move immediately, stops and opens
		// the conflict popup at the first destination that already exists (unless a prior "Apply to
		// all" choice already covers it). Called once per frame - no-op when the queue is empty and
		// no popup is open.
		void processPasteQueue();
		void beginCopyOrCut(bool isCut);
		void beginPasteInto(const Path &targetDirectory);
		// Drag-and-drop between folders (mouse drag, as opposed to beginPasteInto's Ctrl+C/X/V and
		// context-menu path) - always a move, reuses the same m_PasteQueue/conflict-popup machinery
		// so a drop lands through the identical Overwrite/Skip/Rename flow as a cut-paste would.
		void queueDroppedMove(const std::vector<Path> &sources, const Path &targetDirectory);
		void pushNotification(const std::string &message, bool isError);

		Ref<EventBus> m_EventBus;
		std::vector<ProjectRootEntry> m_Roots;
		// Keyboard-nav anchor (Up/Down/Left/Right) and Shift-click range anchor - the "primary"
		// selection. m_SelectedPaths below is the full multi-select set; a plain click collapses
		// both to the same single entry.
		std::string m_SelectedPath;
		std::vector<std::string> m_SelectedPaths;
		// The row Up/Down arrow keys actually move - equal to m_SelectedPath except mid-Shift+Up/Down,
		// where the anchor (m_SelectedPath) holds still and this is the moving far end of the range
		// (see handleKeyboardNavigation). Also what m_ScrollToSelectedPending scrolls to.
		std::string m_KeyboardCursorPath;
		// Absent = collapsed (matches the old implicit-default-closed TreeNodeEx behavior).
		std::unordered_map<std::string, bool> m_ExpandedPaths;
		// Rebuilt fresh at the top of every Render() by rebuildVisibleFlatList(), in on-screen order -
		// only entries under currently-expanded folders appear, same as what's actually
		// visible/clickable.
		std::vector<Path> m_VisibleFlatList;
		// Set for one frame whenever keyboard navigation moves the selection, so the newly-selected
		// row can scroll itself into view (ImGui doesn't do this automatically) - without this,
		// Down/Up across a root-section boundary looked "stuck" once the target row was off-screen
		// (2026-08-28 feedback: "nie mozna strzalkami przechodzic miedzy dyskami").
		bool m_ScrollToSelectedPending = false;
		// See RequestFilePick() above.
		bool m_FilePickModeActive = false;
		// Which button is keyboard-highlighted in whichever modal popup is currently open - shared
		// across all of them since at most one is ever open at a time (see modalButton() above).
		int m_ModalHighlightedButton = 0;

		// "+ Add Root" popup state - path is picked synchronously (native dialog), label is typed
		// afterwards so it defaults to something sensible without forcing a second click-through.
		bool m_AddRootPopupOpen = false;
		Path m_AddRootPendingPath;
		std::array<char, 256> m_AddRootLabelBuffer{};

		// "New Folder.../New File.../Create Defect..." popup - one shared popup, m_CreatePopupKind
		// picks which. Defect = a folder plus stub POSCAR/KPOINTS files inside it (empty for now -
		// more calc-input files land here as that workflow gets built out, 2026-08-29 request).
		enum class CreateEntryKind { Folder, File, Defect };
		bool m_CreatePopupOpen = false;
		CreateEntryKind m_CreatePopupKind = CreateEntryKind::Folder;
		Path m_CreatePopupParent;
		std::array<char, 256> m_CreateNameBuffer{};

		// "Rename" popup (F2 / context menu) - single target only, matches Explorer (renaming N
		// selected items at once isn't offered).
		bool m_RenamePopupOpen = false;
		Path m_RenamePopupTarget;
		std::array<char, 256> m_RenameNameBuffer{};

		// "Delete" confirm popup (Del / context menu) - lists every path about to be removed.
		bool m_DeleteConfirmPopupOpen = false;
		std::vector<Path> m_DeleteConfirmTargets;

		// One entry per file/folder queued by Paste - drained by processPasteQueue(). isCut marks a
		// move (source removed after a successful copy) vs a plain copy.
		struct PendingPasteOperation
		{
			Path source;
			Path destinationDirectory;
			bool isCut = false;
		};
		std::vector<PendingPasteOperation> m_PasteQueue;
		// Conflict resolution for the item currently at the front of m_PasteQueue - Overwrite/Skip
		// apply to every remaining conflict once applyToAll is checked; Rename only ever applies to
		// the one item on screen (bulk-renaming N conflicting items isn't offered - see the .cpp).
		bool m_PasteConflictPopupOpen = false;
		bool m_PasteConflictApplyToAll = false;
		enum class PasteConflictChoice { None, Overwrite, Skip };
		PasteConflictChoice m_PasteConflictAppliedChoice = PasteConflictChoice::None;
		std::array<char, 256> m_PasteConflictRenameBuffer{};
	};
} // namespace DefectStudio
