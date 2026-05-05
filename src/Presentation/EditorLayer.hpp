#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Layer.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "Presentation/Panels/PanelRegistry.hpp"
#include "Presentation/EditorUiState.hpp"
#include "Presentation/Panels/LoggingPanel.hpp"
#include "Presentation/Panels/ProgressMonitorWindow.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"
#include "Presentation/Panels/TaskMonitorWindow.hpp"

namespace DefectStudio
{
	class EventBus;
	class LogRegistry;
	class CommandService;
	class CommandRegistry;
	class ContextManager;
	class KeymapResolver;
	struct CommandID;
	struct ApplicationConfig;

	namespace AppEvents::Config
	{
		struct Applied;
	}

	namespace AppEvents
	{
		struct OpenCommandPaletteRequested;
	}

	class EditorLayer final : public Layer, public EventReceiver
	{
	public:
		EditorLayer();
		void BindRuntimeServices(Ref<EventBus> eventBus,
		                         WeakRef<JobSystem> jobSystem,
		                         WeakRef<ProgressTracker> progressTracker,
		                         Ref<LogRegistry> logRegistry,
		                         WeakRef<CommandService> commandService,
		                         WeakRef<KeymapResolver> keymapResolver,
		                         WeakRef<ContextManager> contextManager,
		                         WeakRef<CommandRegistry> commandRegistry);
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
		void renderCommandPalettePopup();
		void executeCommandFromPalette(const CommandID &id);
		void bindConfigEvents();
		void applyConfigToUiState(const ApplicationConfig &config);
		void onConfigApplied(const AppEvents::Config::Applied &event);
		void onOpenCommandPaletteRequested(const AppEvents::OpenCommandPaletteRequested &event);

	private:
		PanelRegistry m_Panels;
		bool m_PanelsInitialized = false;
		Ref<EventBus> m_EventBus;
		Ref<LogRegistry> m_LogRegistry;
		WeakRef<JobSystem> m_JobSystem;
		WeakRef<ProgressTracker> m_ProgressTracker;
		WeakRef<CommandService> m_CommandService;
		WeakRef<KeymapResolver> m_KeymapResolver;
		WeakRef<ContextManager> m_ContextManager;
		WeakRef<CommandRegistry> m_CommandRegistry;
		Ref<EditorUiState> m_UiState;
		Ref<ApplicationConfig> m_CurrentConfig;
		bool m_CommandPaletteOpenRequested = false;
		int m_CommandPaletteSelection = 0;
		std::array<char, 128> m_CommandPaletteSearchBuffer{};
	};

	template <typename TPanel, typename... Args>
	PanelId EditorLayer::registerPanel(Args &&...args)
	{
		return m_Panels.Add<TPanel>(std::forward<Args>(args)...);
	}
} // namespace DefectStudio
