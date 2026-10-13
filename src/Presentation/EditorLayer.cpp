#include "Core/dspch.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Commands/CommandService.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Domain/DomainLayer.hpp"
#include "Events/EditorUiEvents.hpp"
#include "Events/ProjectEvents.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/PoscarWriter.hpp"
#include "IO/POTCARWriter.hpp"
#include "IO/ProjectManifestIO.hpp"
#include "IO/EnergeticsIO.hpp"
#include "IO/FnvCorrectionsIO.hpp"
#include "IO/ProjectRootsIO.hpp"
#include "IO/RecentProjectsIO.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "IO/TextFileIO.hpp"
#include "Presentation/ProjectSceneSave.hpp"
#include "Renderer/ProjectSceneWindow.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/BondSettingsPanel.hpp"
#include "Presentation/Panels/NewStructureWizardPanel.hpp"
#include "Presentation/Panels/CalculatorConsolePanel.hpp"
#include "Presentation/Panels/ElectronicStructurePanel.hpp"
#include "Presentation/Panels/ElectronicStructureSession.hpp"
#include "Presentation/Panels/SceneDensityLoader.hpp"
#include "Presentation/Panels/ExportImagePanel.hpp"
#include "Presentation/Panels/OccupationDiagramPanel.hpp"
#include "Presentation/Panels/DisplacementComparisonPanel.hpp"
#include "Presentation/Panels/GroupTheoryPanel.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"
#include "Presentation/Panels/DefectEnergeticsSession.hpp"
#include "Presentation/Panels/CalculationSummarySession.hpp"
#include "Presentation/Panels/ElementCatalogPanel.hpp"
#include "Presentation/Panels/ObjectPropertiesPanel.hpp"
#include "Presentation/Panels/RendererPanel.hpp"
#include "Presentation/Panels/SceneOutlinerPanel.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"
#include "Presentation/Panels/MaterialsCollectionPanel.hpp"
#include "Presentation/Panels/StructureCreationTabsPanel.hpp"
#include "Presentation/Panels/StructureHubPanel.hpp"
#include "Presentation/Panels/TerminalPanel.hpp"
#include "Presentation/Panels/TextEditorPanel.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"

namespace DefectStudio
{
	EditorLayer::EditorLayer() : Layer("EditorLayer")
	{
	}

	void EditorLayer::BindRuntimeServices(Ref<EventBus> eventBus,
	                                      WeakRef<JobSystem> jobSystem,
	                                      WeakRef<ProgressTracker> progressTracker,
	                                      Ref<LogRegistry> logRegistry,
	                                      WeakRef<CommandService> commandService,
	                                      WeakRef<KeymapResolver> keymapResolver,
	                                      WeakRef<ContextManager> contextManager,
	                                      WeakRef<CommandRegistry> commandRegistry,
	                                      WeakRef<RendererLayer> rendererLayer,
	                                      AtomStyleTable atomStyleTable,
	                                      Path atomStylesPath,
	                                      WeakRef<DomainLayer> domainLayer,
	                                      ElementPropertiesTable elementPropertiesTable,
	                                      Ref<CreationSessionRegistry> creationSessionRegistry)
	{
		m_EventBus = std::move(eventBus);
		m_LogRegistry = std::move(logRegistry);
		m_JobSystem = std::move(jobSystem);
		m_ProgressTracker = std::move(progressTracker);
		m_CommandService = std::move(commandService);
		m_KeymapResolver = std::move(keymapResolver);
		m_ContextManager = std::move(contextManager);
		m_CommandRegistry = std::move(commandRegistry);
		m_RendererLayer = std::move(rendererLayer);
		m_AtomStyleTable = std::move(atomStyleTable);
		m_AtomStylesPath = std::move(atomStylesPath);
		m_DomainLayer = std::move(domainLayer);
		m_ElementPropertiesTable = std::move(elementPropertiesTable);
		m_CreationSessionRegistry = std::move(creationSessionRegistry);
		bindConfigEvents();
		bindProjectRootEvents();
		DS_LOG_INFO(
			"EditorLayer runtime services bound: event_bus={} job_system={} progress_tracker={}",
			m_EventBus != nullptr,
			!m_JobSystem.expired(),
			!m_ProgressTracker.expired());
	}

	WeakRef<EditorUiState> EditorLayer::GetUiStateHandle() const
	{
		if (m_UiState == nullptr)
			return {};

		return CreateWeakRef(m_UiState);
	}

	void EditorLayer::ApplyConfig(const ApplicationConfig &config)
	{
		m_CurrentConfig = CreateRef<ApplicationConfig>(config);
		applyConfigToUiState(config);
		DS_LOG_INFO(
			"EditorLayer config applied to UI state: font_scale={} workers={} layout={}",
			config.ui.fontScale,
			config.jobs.defaultWorkerThreadCount,
			config.layout.imGuiIniPath.empty() ? "<default>" : config.layout.imGuiIniPath);
	}

	void EditorLayer::ExportConfig(ApplicationConfig &config) const
	{
		if (m_UiState == nullptr)
		{
			DS_LOG_WARN("EditorLayer config export skipped: UI state unavailable");
			return;
		}

		config.ui.fontScale = std::clamp(m_UiState->fontScale, config.ui.fontScaleMin, config.ui.fontScaleMax);
		config.ui.fontScaleStep = std::clamp(m_UiState->fontScaleStep, config.ui.fontScaleStepMin, config.ui.fontScaleStepMax);
		config.ui.fontPath = m_UiState->selectedFontPath;
		config.jobs.defaultWorkerThreadCount = std::max(1, m_UiState->defaultWorkerThreadCount);
		config.jobs.reserveUrgentWorker = m_UiState->reserveUrgentWorkerByDefault;
		config.appearance = m_UiState->appearance;
		config.layout.imGuiIniPath = m_UiState->layoutPath;
		DS_LOG_INFO(
			"EditorLayer config exported from UI state: font_scale={} workers={} layout={}",
			config.ui.fontScale,
			config.jobs.defaultWorkerThreadCount,
			config.layout.imGuiIniPath.empty() ? "<default>" : config.layout.imGuiIniPath);
	}

	void EditorLayer::OnAttach()
	{
		DS_LOG_INFO("EditorLayer attached");
		m_UiState = CreateRef<EditorUiState>();
		if (ImGui::GetCurrentContext() != nullptr)
			m_UiState->fontScale = std::clamp(ImGui::GetIO().FontGlobalScale, m_UiState->fontScaleMin, m_UiState->fontScaleMax);
	}

	void EditorLayer::OnDetach()
	{
		stopPhononAnimations();
		DS_LOG_INFO("EditorLayer detached");
		saveProjectWindowState();
		savePanelVisibilityState();
		m_Panels.Clear();
		m_DefectEnergeticsSession.reset();
		m_CalculationSummarySession.reset();
		m_PhononSession.reset();
		m_PanelsInitialized = false;
		ClearSubscriptions();
		m_EventBus.reset();
		m_LogRegistry.reset();
		m_JobSystem.reset();
		m_ProgressTracker.reset();
		m_CommandService.reset();
		m_KeymapResolver.reset();
		m_ContextManager.reset();
		m_CommandRegistry.reset();
		m_RendererLayer.reset();
		m_UiState.reset();
		m_CurrentConfig.reset();
	}

	void EditorLayer::OnEvent(Event &event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent &keyEvent) {
			handleFontShortcuts(keyEvent);
			return keyEvent.handled;
		});
		dispatcher.Dispatch<KeyRepeatedEvent>([this](KeyRepeatedEvent &keyEvent) {
			handleFontShortcuts(keyEvent);
			return keyEvent.handled;
		});
	}

	void EditorLayer::OnImGuiRender()
	{
		initializePanelsIfNeeded();
		pollPendingWindowRestores();
		renderMainMenuBar();
		renderCommandPalettePopup();
		if (m_SceneDensityLoader != nullptr)
			m_SceneDensityLoader->Update();

		// Session commits happen on this main-thread boundary, once per frame even with closed views.
		if (m_DefectEnergeticsSession)
			m_DefectEnergeticsSession->Update();
		if (m_CalculationSummarySession)
			m_CalculationSummarySession->Update();
		if (m_PhononSession)
			if (const auto renderer = m_RendererLayer.lock())
			{
				const bool anyVisible = std::ranges::any_of(m_Panels.Entries(), [this](const auto &entry) {
					const auto panel = std::dynamic_pointer_cast<PhononModesPanel>(entry.panel);
					return panel && panel->Session() == m_PhononSession && panel->IsVisible();
				});
				m_PhononSession->Update(renderer->GetWindows(), ImGui::GetIO().DeltaTime, anyVisible);
			}
		for (auto &entry : m_Panels.Entries())
		{
			dockAnalysisPanelOnFirstUse(*entry.panel);
			entry.panel->Render();
		}
		if (m_DefectEnergeticsSession)
			m_DefectEnergeticsSession->FlushChanges();
		if (m_PhononSession) m_PhononSession->FlushChanges();
		if (m_PendingPanelFocusTitle.has_value()
			&& ImGui::FindWindowByName(m_PendingPanelFocusTitle->c_str()) != nullptr)
		{
			ImGui::SetWindowFocus(m_PendingPanelFocusTitle->c_str());
			m_PendingPanelFocusTitle.reset();
		}
	}

	WeakRef<IPanel> EditorLayer::findPanel(PanelId panelId)
	{
		return m_Panels.Get(panelId);
	}

	WeakRef<const IPanel> EditorLayer::findPanel(PanelId panelId) const
	{
		return m_Panels.Get(panelId);
	}

	void EditorLayer::handleFontShortcuts(Event &event)
	{
		using namespace EditorUiEvents;

		const bool ctrlPressed = HasModifier(Input::GetCurrentKeyModifiers(), KeyModifiers::Ctrl);
		if (!ctrlPressed)
			return;

		const auto handleKey = [this, &event](int keyCode) {
			if (m_UiState == nullptr)
				return false;

			const float fontScaleMin = m_UiState->fontScaleMin;
			const float fontScaleMax = std::max(m_UiState->fontScaleMax, fontScaleMin);
			const float fontScaleStepMin = m_UiState->fontScaleStepMin;
			const float fontScaleStepMax = std::max(m_UiState->fontScaleStepMax, fontScaleStepMin);
			const float fontScaleStep = std::clamp(m_UiState->fontScaleStep, fontScaleStepMin, fontScaleStepMax);
			m_UiState->fontScaleStep = fontScaleStep;

			if (keyCode == ToNativeKeyCode(KeyCode::Minus))
			{
				m_UiState->fontScale = std::clamp(m_UiState->fontScale - fontScaleStep, fontScaleMin, fontScaleMax);
				if (m_EventBus != nullptr)
					m_EventBus->Queue(FontScaleChanged{m_UiState->fontScale});
				event.handled = true;
				return true;
			}

			if (keyCode == ToNativeKeyCode(KeyCode::Equal))
			{
				m_UiState->fontScale = std::clamp(m_UiState->fontScale + fontScaleStep, fontScaleMin, fontScaleMax);
				if (m_EventBus != nullptr)
					m_EventBus->Queue(FontScaleChanged{m_UiState->fontScale});
				event.handled = true;
				return true;
			}

			return false;
		};

		if (const auto *keyPressed = dynamic_cast<const KeyPressedEvent *>(&event))
			handleKey(keyPressed->GetKeyCode());
		else if (const auto *keyRepeated = dynamic_cast<const KeyRepeatedEvent *>(&event))
			handleKey(keyRepeated->GetKeyCode());
	}

	void EditorLayer::pushPseudopotentialStateToWizard(bool configured)
	{
		Ref<IPanel> panel = findPanel(m_NewStructureWizardPanelId).lock();
		if (auto *wizard = dynamic_cast<NewStructureWizardPanel *>(panel.get()); wizard != nullptr)
			wizard->SetPseudopotentialDirConfigured(configured);
	}

	void EditorLayer::applyConfigToUiState(const ApplicationConfig &config)
	{
		// Before the m_UiState guard below: the wizard needs this whether or not the UI state exists.
		pushPseudopotentialStateToWizard(!config.ui.pseudopotentialDir.Empty());

		if (m_UiState == nullptr)
		{
			DS_LOG_WARN("EditorLayer config apply skipped: UI state unavailable");
			return;
		}

		m_UiState->fontScale = config.ui.fontScale;
		m_UiState->fontScaleStep = config.ui.fontScaleStep;
		m_UiState->fontScaleMin = config.ui.fontScaleMin;
		m_UiState->fontScaleMax = config.ui.fontScaleMax;
		m_UiState->fontScaleStepMin = config.ui.fontScaleStepMin;
		m_UiState->fontScaleStepMax = config.ui.fontScaleStepMax;
		m_UiState->fontScaleStepSliderMax = config.ui.fontScaleStepSliderMax;
		m_UiState->defaultWorkerThreadCount = config.jobs.defaultWorkerThreadCount;
		m_UiState->reserveUrgentWorkerByDefault = config.jobs.reserveUrgentWorker;
		m_UiState->selectedFontPath = config.ui.fontPath;
		m_UiState->paths = config.paths;
		m_UiState->appearance = config.appearance;
		m_UiState->layoutPath = config.layout.imGuiIniPath;
		if (m_UiState->themeSavePath.empty())
			m_UiState->themeSavePath = (config.paths.themesDirectory / Path("dark-orange.yaml")).String();
		if (m_UiState->themeLoadPath.empty())
			m_UiState->themeLoadPath = m_UiState->themeSavePath;
	}

	void EditorLayer::onConfigApplied(const AppEvents::Config::Applied &event)
	{
		DS_LOG_INFO("EditorLayer received config applied event: persisted={}", event.persisted);
		m_CurrentConfig = CreateRef<ApplicationConfig>(event.config);
		applyConfigToUiState(event.config);
	}

} // namespace DefectStudio
