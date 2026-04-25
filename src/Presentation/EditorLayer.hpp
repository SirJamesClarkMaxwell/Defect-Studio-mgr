#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Layer.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "Presentation/Panels/PanelRegistry.hpp"
#include "Presentation/EditorUiState.hpp"
#include "Presentation/Panels/AppearanceEditor.hpp"
#include "Presentation/Panels/ProgressMonitorWindow.hpp"
#include "Presentation/Panels/Settings.hpp"
#include "Presentation/Panels/TaskMonitorWindow.hpp"

namespace DefectStudio
{
	class EventBus;
	struct ApplicationConfig;

	namespace AppEvents::Config
	{
		struct Applied;
	}

	class EditorLayer final : public Layer, public EventReceiver
	{
	public:
		EditorLayer();
		void BindRuntimeServices(Ref<EventBus> eventBus,
		                         WeakRef<JobSystem> jobSystem,
		                         WeakRef<ProgressTracker> progressTracker);
		[[nodiscard]] WeakRef<EditorUiState> GetUiStateHandle() const;
		void ApplyConfig(const ApplicationConfig &config);
		void ExportConfig(ApplicationConfig &config) const;

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
		void initializePanelsIfNeeded();
		void handleFontShortcuts(Event &event);
		void bindConfigEvents();
		void applyConfigToUiState(const ApplicationConfig &config);
		void onConfigApplied(const AppEvents::Config::Applied &event);

	private:
		PanelRegistry m_Panels;
		bool m_PanelsInitialized = false;
		Ref<EventBus> m_EventBus;
		WeakRef<JobSystem> m_JobSystem;
		WeakRef<ProgressTracker> m_ProgressTracker;
		Ref<EditorUiState> m_UiState;
	};

	template <typename TPanel, typename... Args>
	PanelId EditorLayer::registerPanel(Args &&...args)
	{
		return m_Panels.Add<TPanel>(std::forward<Args>(args)...);
	}
} // namespace DefectStudio
