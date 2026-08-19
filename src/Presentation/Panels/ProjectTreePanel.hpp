#pragma once

#include <string>

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

		Ref<EventBus> m_EventBus;
		Path m_RootPath;
	};
} // namespace DefectStudio
