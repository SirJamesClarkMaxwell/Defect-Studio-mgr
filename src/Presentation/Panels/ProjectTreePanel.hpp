#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class EventBus;

	// Filesystem tree over a user-picked directory (local or a mounted drive - mount transparency
	// means this panel doesn't know or care which). Scope cut: no manifest.yaml, no recent-projects
	// list - see docs/work/project/TODO.md T07.5.1 for the full future project system this is not.
	class ProjectTreePanel final : public IPanel
	{
	public:
		explicit ProjectTreePanel(
			Ref<EventBus> eventBus,
			std::string title = "Project Tree",
			bool visibleByDefault = true,
			Path initialRootPath = {});
		ProjectTreePanel(const ProjectTreePanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void renderPickRootButton();
		void renderDirectoryContents(const Path &directory);
		void renderDirectoryContextMenu(const Path &directory);
		// VSCode-style: Up/Down move selection, Right expands, Left collapses (or jumps to parent
		// if already collapsed/a file), Enter toggles a folder, Shift+Enter opens the selected
		// folder as a defect (same action as the RMB menu). Reads m_VisibleFlatList as it stood
		// after the previous frame's render, then updates m_SelectedPath/m_ExpandedPaths - the
		// tree render right after picks those up via SetNextItemOpen.
		void handleKeyboardNavigation();
		void openDefectAt(const Path &directory);

		Ref<EventBus> m_EventBus;
		Path m_RootPath;
		std::string m_SelectedPath;
		// Absent = collapsed (matches the old implicit-default-closed TreeNodeEx behavior).
		std::unordered_map<std::string, bool> m_ExpandedPaths;
		// Rebuilt every renderDirectoryContents() pass, in on-screen order - only entries under
		// currently-expanded folders appear, same as what's actually visible/clickable.
		std::vector<Path> m_VisibleFlatList;
	};
} // namespace DefectStudio
