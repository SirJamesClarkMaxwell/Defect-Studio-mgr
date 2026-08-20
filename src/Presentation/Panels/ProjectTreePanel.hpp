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

	private:
		void renderToolbar();
		void renderAddRootPopup();
		void renderRootSection(const ProjectRootEntry &section);
		void renderRootSectionContextMenu(const ProjectRootEntry &section);
		void renderDirectoryContents(const Path &directory);
		void renderDirectoryContextMenu(const Path &directory);
		// VSCode-style: Up/Down move selection, Right expands, Left collapses (or jumps to parent
		// if already collapsed/a file), Enter toggles a folder, Shift+Enter opens the selected
		// folder as a defect (same action as the RMB menu). Reads m_VisibleFlatList as it stood
		// after the previous frame's render, then updates m_SelectedPath/m_ExpandedPaths - the
		// tree render right after picks those up via SetNextItemOpen. Both maps are keyed by
		// absolute filesystem path, already globally unique across every section, so none of this
		// needs to be per-section.
		void handleKeyboardNavigation();
		void openDefectAt(const Path &directory);

		Ref<EventBus> m_EventBus;
		std::vector<ProjectRootEntry> m_Roots;
		std::string m_SelectedPath;
		// Absent = collapsed (matches the old implicit-default-closed TreeNodeEx behavior).
		std::unordered_map<std::string, bool> m_ExpandedPaths;
		// Rebuilt every Render() pass across all sections, in on-screen order - only entries under
		// currently-expanded folders appear, same as what's actually visible/clickable.
		std::vector<Path> m_VisibleFlatList;

		// "+ Add Root" popup state - path is picked synchronously (native dialog), label is typed
		// afterwards so it defaults to something sensible without forcing a second click-through.
		bool m_AddRootPopupOpen = false;
		Path m_AddRootPendingPath;
		std::array<char, 256> m_AddRootLabelBuffer{};
	};
} // namespace DefectStudio
