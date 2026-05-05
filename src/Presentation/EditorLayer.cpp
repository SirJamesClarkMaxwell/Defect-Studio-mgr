#include "Core/dspch.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Commands/CommandService.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/EditorUiEvents.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"

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

		struct PaletteCommandRow
		{
			CommandID id;
			std::string name;
			std::string category;
			std::string description;
			std::string shortcut;
			bool enabled = true;
		};

		struct PaletteInputState
		{
			int navigationDelta = 0;
		};

		[[nodiscard]] std::string toLowerCopy(std::string_view value)
		{
			std::string result(value);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return result;
		}

		[[nodiscard]] bool containsIgnoreCase(std::string_view haystack, std::string_view needle)
		{
			if (needle.empty())
				return true;
			const std::string lowerHaystack = toLowerCopy(haystack);
			const std::string lowerNeedle = toLowerCopy(needle);
			return lowerHaystack.find(lowerNeedle) != std::string::npos;
		}

		[[nodiscard]] std::string findShortcutForCommand(
			const CommandID &id,
			const KeymapResolver *resolver,
			const ContextManager *contextManager)
		{
			if (resolver == nullptr)
				return {};

			ContextManager emptyContext;
			const ContextManager &activeContext = contextManager == nullptr ? emptyContext : *contextManager;
			for (const KeyBinding &binding : resolver->ListBindings())
			{
				if (!binding.enabled || binding.commandId.value != id.value || !binding.when.Matches(activeContext))
					continue;
				return ToString(binding.chord);
			}
			return {};
		}

		[[nodiscard]] bool matchesPaletteQuery(const PaletteCommandRow &row, std::string_view query)
		{
			return containsIgnoreCase(row.name, query)
				|| containsIgnoreCase(row.category, query)
				|| containsIgnoreCase(row.description, query)
				|| containsIgnoreCase(row.id.value, query)
				|| containsIgnoreCase(row.shortcut, query);
		}

		int commandPaletteInputCallback(ImGuiInputTextCallbackData *data)
		{
			auto *state = static_cast<PaletteInputState *>(data->UserData);
			if (state == nullptr || data->EventFlag != ImGuiInputTextFlags_CallbackHistory)
				return 0;

			if (data->EventKey == ImGuiKey_UpArrow)
				state->navigationDelta = -1;
			else if (data->EventKey == ImGuiKey_DownArrow)
				state->navigationDelta = 1;
			return 0;
		}
	}

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
	                                      WeakRef<CommandRegistry> commandRegistry)
	{
		m_EventBus = std::move(eventBus);
		m_LogRegistry = std::move(logRegistry);
		m_JobSystem = std::move(jobSystem);
		m_ProgressTracker = std::move(progressTracker);
		m_CommandService = std::move(commandService);
		m_KeymapResolver = std::move(keymapResolver);
		m_ContextManager = std::move(contextManager);
		m_CommandRegistry = std::move(commandRegistry);
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
		DS_LOG_INFO("EditorLayer detached");
		m_Panels.Clear();
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
		renderMainMenuBar();
		renderCommandPalettePopup();

		for (auto &entry : m_Panels.Entries())
		{
			entry.panel->Render();
		}
	}

	void EditorLayer::initializePanelsIfNeeded()
	{
		if (m_PanelsInitialized)
			return;

		registerPanel<ProgressMonitorWindow>(m_EventBus, m_ProgressTracker, "Progress Monitor", true);
		registerPanel<TaskMonitorWindow>(m_EventBus, m_JobSystem, "Task Monitor", true);
		registerPanel<LoggingPanel>(m_LogRegistry, "Logging Panel", true);
		registerPanel<SettingsPanel>(
			m_EventBus,
			m_JobSystem,
			CreateWeakRef(m_UiState),
			m_KeymapResolver,
			m_CommandRegistry,
			m_CurrentConfig != nullptr ? *m_CurrentConfig : ApplicationConfig{},
			"SettingsPanel",
			true);
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

	void EditorLayer::renderCommandPalettePopup()
	{
		if (m_CommandPaletteOpenRequested)
		{
			ImGui::OpenPopup("Command Palette");
			m_CommandPaletteOpenRequested = false;
			m_CommandPaletteSelection = 0;
		}

		bool open = true;
		if (!ImGui::BeginPopupModal("Command Palette", &open, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		auto commandRegistry = m_CommandRegistry.lock();
		auto commandService = m_CommandService.lock();
		auto keymapResolver = m_KeymapResolver.lock();
		auto contextManager = m_ContextManager.lock();
		if (commandRegistry == nullptr || commandService == nullptr)
		{
			ImGui::TextDisabled("Command runtime unavailable.");
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();

		PaletteInputState inputState;
		ImGui::SetNextItemWidth(520.0f);
		const bool enterPressed = ImGui::InputTextWithHint(
			"##command_palette_search",
			"Search commands",
			m_CommandPaletteSearchBuffer.data(),
			m_CommandPaletteSearchBuffer.size(),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
			commandPaletteInputCallback,
			&inputState);

		std::vector<PaletteCommandRow> rows;
		for (const CommandMeta &meta : commandRegistry->ListCommands())
		{
			if (HasFlag(meta.flags, CommandFlags::HiddenFromPalette))
				continue;

			PaletteCommandRow row;
			row.id = meta.id;
			row.name = meta.name;
			row.category = meta.category;
			row.description = meta.description;
			row.shortcut = findShortcutForCommand(
				meta.id,
				keymapResolver.get(),
				contextManager.get());
			row.enabled = commandService->CanExecute(meta.id).HasValue();
			if (matchesPaletteQuery(row, m_CommandPaletteSearchBuffer.data()))
				rows.push_back(std::move(row));
		}

		std::sort(rows.begin(), rows.end(), [](const PaletteCommandRow &lhs, const PaletteCommandRow &rhs) {
			if (lhs.category == rhs.category)
				return lhs.name < rhs.name;
			return lhs.category < rhs.category;
		});

		if (m_CommandPaletteSelection >= static_cast<int>(rows.size()))
			m_CommandPaletteSelection = std::max(0, static_cast<int>(rows.size()) - 1);

		if (inputState.navigationDelta != 0 && !rows.empty())
			m_CommandPaletteSelection = std::clamp(
				m_CommandPaletteSelection + inputState.navigationDelta,
				0,
				static_cast<int>(rows.size()) - 1);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();

		ImGui::Separator();
		if (ImGui::BeginChild("##command_palette_results", ImVec2(640.0f, 320.0f), true))
		{
			for (int index = 0; index < static_cast<int>(rows.size()); ++index)
			{
				const PaletteCommandRow &row = rows[static_cast<std::size_t>(index)];
				const std::string label = row.name
					+ (row.shortcut.empty() ? "" : "    " + row.shortcut)
					+ "##" + row.id.value;
				const bool selected = index == m_CommandPaletteSelection;
				if (selected)
				{
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.14f, 0.17f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.18f, 0.22f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.10f, 0.12f, 0.15f, 1.0f));
				}
				ImGui::BeginDisabled(!row.enabled);
				if (ImGui::Selectable(label.c_str(), selected))
				{
					executeCommandFromPalette(row.id);
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				if (selected)
				{
					if (inputState.navigationDelta != 0)
						ImGui::SetScrollHereY(0.5f);
					ImGui::PopStyleColor(3);
				}
				if (!row.description.empty())
					ImGui::TextDisabled("%s", row.description.c_str());
			}

			if (rows.empty())
				ImGui::TextDisabled("No commands found.");
		}
		ImGui::EndChild();

		if (enterPressed && !rows.empty())
		{
			const PaletteCommandRow &row = rows[static_cast<std::size_t>(m_CommandPaletteSelection)];
			if (row.enabled)
			{
				executeCommandFromPalette(row.id);
				ImGui::CloseCurrentPopup();
			}
		}

		if (!open)
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void EditorLayer::executeCommandFromPalette(const CommandID &id)
	{
		auto commandService = m_CommandService.lock();
		if (commandService == nullptr)
			return;

		CommandContext context(ContextID{"editor.command_palette"});
		context.SetSource("EditorLayer command palette");
		auto result = commandService->Execute(id, std::move(context));
		if (!result)
			DS_LOG_WARN("EditorLayer command palette failed: {}", result.Error().technicalDetails);
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
		AddSubscription(subscribeEditorLayer<CoreEvents::OpenCommandPaletteRequested>(
			*m_EventBus,
			*this,
			&EditorLayer::onOpenCommandPaletteRequested,
			EventPriority::High));
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
		m_CurrentConfig = CreateRef<ApplicationConfig>(event.config);
		applyConfigToUiState(event.config);
	}

	void EditorLayer::onOpenCommandPaletteRequested(const CoreEvents::OpenCommandPaletteRequested &)
	{
		m_CommandPaletteOpenRequested = true;
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
