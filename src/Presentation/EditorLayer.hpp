#pragma once

#include <cstdint>
#include <utility>

#include "Core/Layer.hpp"
#include "Presentation/Panels/PanelRegistry.hpp"
#include "Presentation/Panels/ProgressMonitorWindow.hpp"
#include "Presentation/Panels/Settings.hpp"
#include "Presentation/Panels/TaskMonitorWindow.hpp"

namespace DefectStudio
{
	class EditorLayer final : public Layer
	{
	public:
		EditorLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event &event) override;
		void OnImGuiRender() override;

	private:
		template <typename TPanel, typename... Args>
		PanelId registerPanel(Args &&...args);

		WeakRef<IPanel> findPanel(PanelId panelId);
		WeakRef<const IPanel> findPanel(PanelId panelId) const;

		void renderMainMenuBar();
		void renderFileMenu();
		void renderEditMenu();
		void renderViewMenu();
		void renderDefectMenu();
		void renderComputationsMenu();
		void renderToolsMenu();
		void renderHelpMenu();
		void handleFontShortcuts(Event &event);

	private:
		PanelRegistry m_Panels;
	};

	template <typename TPanel, typename... Args>
	PanelId EditorLayer::registerPanel(Args &&...args)
	{
		return m_Panels.Add<TPanel>(std::forward<Args>(args)...);
	}
} // namespace DefectStudio
