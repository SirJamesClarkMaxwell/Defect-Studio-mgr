#include "Core/dspch.hpp"

#include <algorithm>
#include <functional>

#include <imgui.h>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/EditorUiEvents.hpp"
#include "Presentation/EditorLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeEditorLayer(
			EventBus &bus,
			EditorLayer &layer,
			void (EditorLayer::*method)(const EventType &),
			EventPriority priority = EventPriority::Normal)
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &layer), priority);
		}
	}

	EditorLayer::EditorLayer() : Layer("EditorLayer")
	{
	}

	void EditorLayer::BindRuntimeServices(Ref<EventBus> eventBus,
	                                      WeakRef<JobSystem> jobSystem,
	                                      WeakRef<ProgressTracker> progressTracker)
	{
		m_EventBus = std::move(eventBus);
		m_JobSystem = std::move(jobSystem);
		m_ProgressTracker = std::move(progressTracker);
		bindConfigEvents();
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
		DS_LOG_INFO("EditorLayer detached");
		m_Panels.Clear();
		m_PanelsInitialized = false;
		ClearSubscriptions();
		m_EventBus.reset();
		m_JobSystem.reset();
		m_ProgressTracker.reset();
		m_UiState.reset();
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
		renderMainMenuBar();
		for (auto &entry : m_Panels.Entries())
			entry.panel->Render();
	}

	void EditorLayer::initializePanelsIfNeeded()
	{
		if (m_PanelsInitialized)
			return;

		registerPanel<ProgressMonitorWindow>(m_JobSystem, m_ProgressTracker, "Progress Monitor", true);
		registerPanel<TaskMonitorWindow>(m_JobSystem, "Task Monitor", true);
		registerPanel<Settings>(m_EventBus, m_JobSystem, CreateWeakRef(m_UiState), "Settings", true);
		registerPanel<AppearanceEditor>(m_EventBus, CreateWeakRef(m_UiState), "Appearance Editor", false);
		m_PanelsInitialized = true;
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

		const bool ctrlPressed = Input::IsKeyDown(KeyCode::LeftControl) || Input::IsKeyDown(KeyCode::RightControl);
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

	void EditorLayer::bindConfigEvents()
	{
		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("EditorLayer config event binding skipped: EventBus unavailable");
			return;
		}

		using namespace AppEvents::Config;
		AddSubscription(subscribeEditorLayer<Applied>(*m_EventBus, *this, &EditorLayer::onConfigApplied, EventPriority::High));
		DS_LOG_INFO("EditorLayer config event handlers bound");
	}

	void EditorLayer::applyConfigToUiState(const ApplicationConfig &config)
	{
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
		applyConfigToUiState(event.config);
	}

	void EditorLayer::renderMainMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		renderFileMenu();
		renderEditMenu();
		renderViewMenu();
		renderDefectMenu();
		renderComputationsMenu();
		renderToolsMenu();
		renderHelpMenu();

		ImGui::EndMainMenuBar();
	}

	void EditorLayer::renderFileMenu()
	{
		if (!ImGui::BeginMenu("Plik"))
			return;

		ImGui::MenuItem("Nowy", nullptr, false, false);
		ImGui::MenuItem("Otworz", nullptr, false, false);
		ImGui::MenuItem("Zapisz", nullptr, false, false);
		if (ImGui::BeginMenu("Ostatnie projekty"))
		{
			ImGui::MenuItem("Brak ostatnich projektow", nullptr, false, false);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Import DFT"))
		{
			ImGui::MenuItem("VASP", nullptr, false, false);
			ImGui::MenuItem("QE", nullptr, false, false);
			ImGui::MenuItem("FHI-aims", nullptr, false, false);
			ImGui::MenuItem("CP2K", nullptr, false, false);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Eksport"))
		{
			ImGui::MenuItem("JSON", nullptr, false, false);
			ImGui::MenuItem("CSV", nullptr, false, false);
			ImGui::MenuItem("LaTeX", nullptr, false, false);
			ImGui::MenuItem("ZIP", nullptr, false, false);
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	void EditorLayer::renderEditMenu()
	{
		if (!ImGui::BeginMenu("Edycja"))
			return;

		ImGui::MenuItem("Cofnij", "Ctrl+Z", false, false);
		ImGui::MenuItem("Ponow", "Ctrl+Y", false, false);
		if (ImGui::BeginMenu("Clipboard"))
		{
			ImGui::MenuItem("Kopiuj wezel", nullptr, false, false);
			ImGui::MenuItem("Wytnij wezel", nullptr, false, false);
			ImGui::MenuItem("Wklej wezel", nullptr, false, false);
			ImGui::MenuItem("Duplikuj wezel", nullptr, false, false);
			ImGui::EndMenu();
		}
		ImGui::MenuItem("Znajdz defekt", "Ctrl+F", false, false);
		ImGui::MenuItem("Szybka nawigacja", "Ctrl+G", false, false);
		ImGui::EndMenu();
	}

	void EditorLayer::renderViewMenu()
	{
		if (!ImGui::BeginMenu("Widok"))
			return;

		const auto panelIds = m_Panels.GetIds();
		for (const PanelId panelId : panelIds)
		{
			if (auto panel = findPanel(panelId).lock())
			{
				bool visible = panel->IsVisible();
				if (ImGui::MenuItem(panel->GetTitle().c_str(), nullptr, &visible))
					panel->SetVisible(visible);
			}
		}

		if (ImGui::BeginMenu("Klonuj panel"))
		{
			for (const PanelId panelId : panelIds)
			{
				if (auto panel = findPanel(panelId).lock())
				{
					if (ImGui::MenuItem(panel->GetTitle().c_str()))
						(void)m_Panels.Clone(panelId);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Uklad paneli"))
		{
			ImGui::MenuItem("Preset 1", nullptr, false, false);
			ImGui::MenuItem("Preset 2", nullptr, false, false);
			ImGui::MenuItem("Preset 3", nullptr, false, false);
			ImGui::MenuItem("Preset 4", nullptr, false, false);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Styl wizualizacji"))
		{
			ImGui::MenuItem("Ball-stick", nullptr, false, false);
			ImGui::MenuItem("CPK", nullptr, false, false);
			ImGui::MenuItem("Polyhedral", nullptr, false, false);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Nakladki"))
		{
			ImGui::MenuItem("CHGCAR", nullptr, false, false);
			ImGui::MenuItem("Orbital KS", nullptr, false, false);
			ImGui::MenuItem("Spin density", nullptr, false, false);
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	void EditorLayer::renderDefectMenu()
	{
		if (!ImGui::BeginMenu("Defekt"))
			return;

		ImGui::MenuItem("Nowy defekt wizard", "Ctrl+D", false, false);
		ImGui::MenuItem("Generuj supercele per q", nullptr, false, false);
		ImGui::MenuItem("ShakeNBreak", nullptr, false, false);
		ImGui::MenuItem("NEB", nullptr, false, false);
		ImGui::MenuItem("Diagram formacji", nullptr, false, false);
		ImGui::MenuItem("negative-U", nullptr, false, false);
		ImGui::MenuItem("ZPL", nullptr, false, false);
		ImGui::MenuItem("ZFS", nullptr, false, false);
		ImGui::MenuItem("Scorecard", nullptr, false, false);
		ImGui::EndMenu();
	}

	void EditorLayer::renderComputationsMenu()
	{
		if (!ImGui::BeginMenu("Obliczenia"))
			return;

		ImGui::MenuItem("Uruchom", "F5", false, false);
		ImGui::MenuItem("Zatrzymaj", "F7", false, false);
		if (ImGui::BeginMenu("Funkcjonały"))
		{
			ImGui::MenuItem("PBE", nullptr, false, false);
			ImGui::MenuItem("HSE06", nullptr, false, false);
			ImGui::MenuItem("PBE+U", nullptr, false, false);
			ImGui::MenuItem("vdW-DF", nullptr, false, false);
			ImGui::MenuItem("G0W0", nullptr, false, false);
			ImGui::EndMenu();
		}
		ImGui::MenuItem("Korekcja FNV/eFNV", nullptr, false, false);
		ImGui::MenuItem("Menadzer kolejki", nullptr, false, false);
		ImGui::EndMenu();
	}

	void EditorLayer::renderToolsMenu()
	{
		if (!ImGui::BeginMenu("Narzedzia"))
			return;

		ImGui::MenuItem("Baza danych", "Ctrl+B", false, false);
		ImGui::MenuItem("Import z h-bn.info", nullptr, false, false);
		ImGui::MenuItem("QPOD API", nullptr, false, false);
		ImGui::MenuItem("Ranking SPE", nullptr, false, false);
		ImGui::MenuItem("Brouwer", nullptr, false, false);
		ImGui::MenuItem("Kalkulator Purcella", nullptr, false, false);
		ImGui::MenuItem("Preferencje", nullptr, false, false);
		ImGui::EndMenu();
	}

	void EditorLayer::renderHelpMenu()
	{
		if (!ImGui::BeginMenu("Pomoc"))
			return;

		ImGui::MenuItem("Dokumentacja", "F1", false, false);
		ImGui::MenuItem("Tutorial", nullptr, false, false);
		ImGui::MenuItem("Lista skrotow", nullptr, false, false);
		ImGui::EndMenu();
	}

} // namespace DefectStudio
