#include "Core/dspch.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <sstream>
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
#include "Core/Platform/FileDialog.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Events/EditorUiEvents.hpp"
#include "Events/ProjectEvents.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/ProjectManifestIO.hpp"
#include "IO/ProjectRootsIO.hpp"
#include "IO/RecentProjectsIO.hpp"
#include "IO/TextFileIO.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/BondSettingsPanel.hpp"
#include "Presentation/Panels/CalculationSummaryPanel.hpp"
#include "Presentation/Panels/CalculatorConsolePanel.hpp"
#include "Presentation/Panels/ElectronicStructurePanel.hpp"
#include "Presentation/Panels/ElectronicStructureSession.hpp"
#include "Presentation/Panels/ExportImagePanel.hpp"
#include "Presentation/Panels/OccupationDiagramPanel.hpp"
#include "Presentation/Panels/ElementCatalogPanel.hpp"
#include "Presentation/Panels/ObjectPropertiesPanel.hpp"
#include "Presentation/Panels/RendererPanel.hpp"
#include "Presentation/Panels/SceneOutlinerPanel.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"
#include "Presentation/Panels/TerminalPanel.hpp"
#include "Presentation/Panels/TextEditorPanel.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"

namespace DefectStudio
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

	[[nodiscard]] Path ProjectWindowsStatePath()
	{
		return Path::FromResolved(
			FileSystem::CurrentPath() / "install" / "users" / "default" / "config" / "project_windows.txt");
	}

	[[nodiscard]] Path PanelVisibilityStatePath()
	{
		return Path::FromResolved(
			FileSystem::CurrentPath() / "install" / "users" / "default" / "config" / "panel_visibility.txt");
	}

	[[nodiscard]] std::vector<std::string> SplitOn(const std::string &text, char delimiter)
	{
		std::vector<std::string> parts;
		std::size_t start = 0;
		while (true)
		{
			const std::size_t pos = text.find(delimiter, start);
			parts.push_back(text.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
			if (pos == std::string::npos)
				break;
			start = pos + 1;
		}
		return parts;
	}

	// Minimal hand-rolled block format ("key=value" lines, "---" between windows) - not real YAML,
	// deliberately: this file is internal-only (nothing else reads it), and pulling yaml-cpp
	// ceremony into EditorLayer.cpp for one save/load pair isn't worth it. See
	// EditorLayer::saveProjectWindowState/loadAndQueueProjectWindowRestores for how it's used.
	[[nodiscard]] std::string SerializeWindowRecord(const PersistedWindowRecord &record)
	{
		std::ostringstream stream;
		stream << "source=" << record.sourcePath.String() << '\n';
		stream << "display_name=" << record.displayName << '\n';
		stream << "view=" << SerializeViewSnapshot(record.view) << '\n';
		stream << "show=" << record.showAtoms << ',' << record.showBonds << ',' << record.showCellBox << ','
			   << record.showGrid << '\n';
		stream << "orbital_up=" << record.orbitalUpEnabled << ',' << record.orbitalUpPositiveColor.x << ','
			   << record.orbitalUpPositiveColor.y << ',' << record.orbitalUpPositiveColor.z << ','
			   << record.orbitalUpNegativeColor.x << ',' << record.orbitalUpNegativeColor.y << ','
			   << record.orbitalUpNegativeColor.z << ',' << record.orbitalUpAlpha << '\n';
		stream << "orbital_down=" << record.orbitalDownEnabled << ',' << record.orbitalDownPositiveColor.x << ','
			   << record.orbitalDownPositiveColor.y << ',' << record.orbitalDownPositiveColor.z << ','
			   << record.orbitalDownNegativeColor.x << ',' << record.orbitalDownNegativeColor.y << ','
			   << record.orbitalDownNegativeColor.z << ',' << record.orbitalDownAlpha << '\n';
		stream << "has_es=" << record.hasElectronicStructureState << '\n';
		if (record.hasElectronicStructureState)
		{
			stream << "es=" << record.esBandStart << ',' << record.esBandEnd << ',' << record.esGapWindowMargin
				   << ',' << record.esLocalizationThreshold << ',' << record.esSplitSpinChannels << ','
				   << record.esRelativeToVbm << ',' << record.esSelectedBand << ',' << record.esIsoValue << '\n';
			stream << "es_iso_overrides=";
			bool first = true;
			for (const auto &[spinChannel, band, value] : record.esIsoOverrides)
			{
				if (!first)
					stream << ';';
				first = false;
				stream << spinChannel << ':' << band << ':' << value;
			}
			stream << '\n';
		}
		stream << "---\n";
		return stream.str();
	}

	[[nodiscard]] std::vector<PersistedWindowRecord> ParseProjectWindowsState(const std::string &text)
	{
		std::vector<PersistedWindowRecord> records;
		std::unordered_map<std::string, std::string> fields;
		std::istringstream stream(text);
		std::string line;

		const auto flush = [&]() {
			if (!fields.contains("source"))
			{
				fields.clear();
				return;
			}
			PersistedWindowRecord record;
			record.sourcePath = Path(fields["source"]);
			record.displayName = fields.contains("display_name") ? fields["display_name"] : std::string{};
			if (fields.contains("view"))
			{
				if (std::optional<RendererViewSnapshot> snapshot = DeserializeViewSnapshot(fields["view"]))
					record.view = *snapshot;
			}
			if (fields.contains("show"))
			{
				const std::vector<std::string> parts = SplitOn(fields["show"], ',');
				if (parts.size() == 4)
				{
					record.showAtoms = parts[0] == "1";
					record.showBonds = parts[1] == "1";
					record.showCellBox = parts[2] == "1";
					record.showGrid = parts[3] == "1";
				}
			}
			const auto parseOrbital =
				[&](const char *key, bool &enabled, glm::vec3 &pos, glm::vec3 &neg, float &alpha) {
					if (!fields.contains(key))
						return;
					const std::vector<std::string> parts = SplitOn(fields[key], ',');
					if (parts.size() != 8)
						return;
					try
					{
						enabled = parts[0] == "1";
						pos = glm::vec3(std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3]));
						neg = glm::vec3(std::stof(parts[4]), std::stof(parts[5]), std::stof(parts[6]));
						alpha = std::stof(parts[7]);
					}
					catch (const std::exception &)
					{
					}
				};
			parseOrbital(
				"orbital_up", record.orbitalUpEnabled, record.orbitalUpPositiveColor, record.orbitalUpNegativeColor,
				record.orbitalUpAlpha);
			parseOrbital(
				"orbital_down", record.orbitalDownEnabled, record.orbitalDownPositiveColor,
				record.orbitalDownNegativeColor, record.orbitalDownAlpha);

			record.hasElectronicStructureState = fields.contains("has_es") && fields["has_es"] == "1";
			if (record.hasElectronicStructureState && fields.contains("es"))
			{
				const std::vector<std::string> parts = SplitOn(fields["es"], ',');
				if (parts.size() == 8)
				{
					try
					{
						record.esBandStart = std::stoi(parts[0]);
						record.esBandEnd = std::stoi(parts[1]);
						record.esGapWindowMargin = std::stoi(parts[2]);
						record.esLocalizationThreshold = std::stof(parts[3]);
						record.esSplitSpinChannels = parts[4] == "1";
						record.esRelativeToVbm = parts[5] == "1";
						record.esSelectedBand = std::stoi(parts[6]);
						record.esIsoValue = std::stof(parts[7]);
					}
					catch (const std::exception &)
					{
					}
				}
			}
			if (fields.contains("es_iso_overrides") && !fields["es_iso_overrides"].empty())
			{
				for (const std::string &token : SplitOn(fields["es_iso_overrides"], ';'))
				{
					const std::vector<std::string> parts = SplitOn(token, ':');
					if (parts.size() != 3)
						continue;
					try
					{
						record.esIsoOverrides.emplace_back(
							std::stoi(parts[0]), std::stoi(parts[1]), std::stof(parts[2]));
					}
					catch (const std::exception &)
					{
					}
				}
			}

			records.push_back(std::move(record));
			fields.clear();
		};

		while (std::getline(stream, line))
		{
			if (line == "---")
			{
				flush();
				continue;
			}
			const std::size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			fields[line.substr(0, eq)] = line.substr(eq + 1);
		}
		flush();

		return records;
	}

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

	// Deliberately separate from "Open Defect" (geometry only, ProjectTreePanel) - electronic
	// structure is a Python/WAVECAR round-trip the user may not want on every geometry load, so it
	// gets its own explicit trigger instead of firing automatically whenever a window is focused.
	class LoadElectronicStructureForFocusedWindowCommand final : public ICommand
	{
	public:
		explicit LoadElectronicStructureForFocusedWindowCommand(Ref<ElectronicStructureSession> session)
			: m_Session(std::move(session))
		{
		}

		Result<void> Execute(CommandContext &) override
		{
			if (m_Session == nullptr)
				return {};
			RendererWindowState *windowState = m_Session->FindFocusedWindow();
			if (windowState == nullptr)
				return {};
			ElectronicStructureSession::WindowState &state = m_Session->Update(*windowState);
			m_Session->DispatchOutputLoad(state);
			return {};
		}

		std::string Description() const override
		{
			return "Load electronic structure for the focused renderer window";
		}

	private:
		Ref<ElectronicStructureSession> m_Session;
	};

	// Un-hides the panel if the user had closed it, then focuses it - a closed docked window has no
	// ImGui window to focus, so SetVisible must run first (that frame's Render() reopens it, then
	// SetWindowFocus can find it by title).
	class FocusPanelCommand final : public ICommand
	{
	public:
		FocusPanelCommand(WeakRef<IPanel> panel, std::string description)
			: m_Panel(std::move(panel)), m_Description(std::move(description))
		{
		}

		Result<void> Execute(CommandContext &) override
		{
			Ref<IPanel> panel = m_Panel.lock();
			if (panel == nullptr)
				return {};
			panel->SetVisible(true);
			ImGui::SetWindowFocus(panel->GetTitle().c_str());
			return {};
		}

		std::string Description() const override
		{
			return m_Description;
		}

	private:
		WeakRef<IPanel> m_Panel;
		std::string m_Description;
	};

	// Same "reopen if closed, then act" shape as FocusPanelCommand, but for TerminalPanel-
	// specific actions (new tab, close current, split) that don't fit the generic
	// SetVisible+SetWindowFocus pattern.
	class TerminalPanelCommand final : public ICommand
	{
	public:
		TerminalPanelCommand(WeakRef<IPanel> panel, std::function<void(TerminalPanel &)> action, std::string description)
			: m_Panel(std::move(panel)), m_Action(std::move(action)), m_Description(std::move(description))
		{
		}

		Result<void> Execute(CommandContext &) override
		{
			Ref<IPanel> panel = m_Panel.lock();
			if (panel == nullptr)
				return {};
			if (auto *terminal = dynamic_cast<TerminalPanel *>(panel.get()))
				m_Action(*terminal);
			return {};
		}

		std::string Description() const override
		{
			return m_Description;
		}

	private:
		WeakRef<IPanel> m_Panel;
		std::function<void(TerminalPanel &)> m_Action;
		std::string m_Description;
	};

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
	                                      ElementPropertiesTable elementPropertiesTable)
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
		DS_LOG_INFO("EditorLayer detached");
		saveProjectWindowState();
		savePanelVisibilityState();
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
		const Path logExportPath = m_CurrentConfig != nullptr
			? m_CurrentConfig->paths.exportsDirectory / Path("event-log-export.csv")
			: Path("logs") / Path("event-log-export.csv");
		registerPanel<LoggingPanel>(m_EventBus, m_LogRegistry, logExportPath, "Logging Panel", true);
		m_ProjectTreePanelId = registerPanel<ProjectTreePanel>(m_EventBus, "Project Tree", true);
		if (auto commandRegistry = m_CommandRegistry.lock())
		{
			auto result = commandRegistry->Register(
				CommandMeta{
					CommandID{"editor.focus_project_tree"},
					"Focus Project Tree",
					"Editor",
					"Bring the Project Tree panel to focus, reopening it first if it was closed.",
					{},
					CommandFlags::None},
				[this](CommandContext &) -> Unique<ICommand> {
					return CreateUnique<FocusPanelCommand>(findPanel(m_ProjectTreePanelId), "Focus Project Tree");
				});
			if (!result)
				DS_LOG_WARN("Focus Project Tree command registration failed: {}", result.Error().technicalDetails);
		}
		m_TextEditorPanelId = registerPanel<TextEditorPanel>("Text Editor", false);
		m_TerminalPanelId = registerPanel<TerminalPanel>("Terminal", false);
		m_CalculatorConsolePanelId = registerPanel<CalculatorConsolePanel>("Integrated Python Console", false);
		m_CalculationSummaryPanelId =
			registerPanel<CalculationSummaryPanel>(m_JobSystem, "Calculation Summary", false);
		if (auto commandRegistry = m_CommandRegistry.lock())
		{
			const auto registerTerminalCommand =
				[this, &commandRegistry](
					const char *id, const char *name, const char *description, std::function<void(TerminalPanel &)> action) {
					auto result = commandRegistry->Register(
						CommandMeta{CommandID{id}, name, "Terminal", description, {}, CommandFlags::None},
						[this, action = std::move(action), name](CommandContext &) -> Unique<ICommand> {
							return CreateUnique<TerminalPanelCommand>(findPanel(m_TerminalPanelId), action, name);
						});
					if (!result)
						DS_LOG_WARN("{} command registration failed: {}", name, result.Error().technicalDetails);
				};

			registerTerminalCommand(
				"terminal.focus", "Focus Terminal", "Bring the Terminal panel to focus, reopening it first if it was closed.",
				[](TerminalPanel &terminal) { terminal.RequestFocus(); });
			registerTerminalCommand(
				"terminal.new_tab", "New Terminal Tab", "Open a new terminal tab and focus it.",
				[](TerminalPanel &terminal) { terminal.OpenNewTab(); });
			registerTerminalCommand(
				"terminal.close_current", "Close Current Terminal", "Close the focused terminal pane (or tab, if it's the only pane).",
				[](TerminalPanel &terminal) { terminal.CloseActiveTab(); });
			registerTerminalCommand(
				"terminal.split", "Split Terminal", "Split the active terminal tab, opening a new shell alongside the current one.",
				[](TerminalPanel &terminal) { terminal.SplitActivePane(); });
		}
		loadInitialProjectState();
		if (auto rendererLayer = m_RendererLayer.lock())
		{
			registerPanel<RendererPanel>(
				*rendererLayer,
				m_EventBus,
				m_ContextManager,
				m_CommandRegistry,
				"Renderer",
				true);
			registerPanel<SceneOutlinerPanel>(*rendererLayer, "Scene Outliner", true);
			registerPanel<ObjectPropertiesPanel>(*rendererLayer, m_CommandRegistry, m_DomainLayer, "Object Properties", true);
			registerPanel<BondSettingsPanel>(
				*rendererLayer, m_CommandRegistry, m_DomainLayer, m_ElementPropertiesTable, "Bond Settings", false);
			registerPanel<ElementCatalogPanel>(
				*rendererLayer, m_CommandRegistry, m_AtomStyleTable, m_ElementPropertiesTable, m_AtomStylesPath,
				"Element Catalog", false);
			// Shared model/job-dispatch behind both panels below (band data, caches, in-flight
			// jobs) - split into two windows so the occupation plot can fill its own window
			// instead of a fixed height squeezed under a long list of controls. Kept as a member
			// (not local) so saveProjectWindowState/pollPendingWindowRestores can reach it too.
			m_ElectronicStructureSession = CreateRef<ElectronicStructureSession>(*rendererLayer, m_JobSystem);
			// Applied here (not in loadInitialProjectState(), which runs before this session
			// exists) - the active project's manifest, if any, owns the bulk reference now (see
			// T07.5.5); ElectronicStructureSession's own persisted-defaults file stays as a
			// same-machine convenience fallback for when no project is open.
			if (m_ActiveProject.has_value())
				m_ElectronicStructureSession->SetIrrepLabelOverrides(m_ActiveProject->irrepLabelOverrides);
			if (m_ActiveProject.has_value() && !m_ActiveProject->bulkDirectory.Empty())
			{
				m_ElectronicStructureSession->SetBulkDirectory(m_ActiveProject->bulkDirectory);
				m_ElectronicStructureSession->DispatchBulkLoad();
			}
			registerPanel<ElectronicStructurePanel>(
				m_ElectronicStructureSession,
				m_EventBus,
				"Electronic Structure",
				true);
			registerPanel<OccupationDiagramPanel>(
				m_ElectronicStructureSession,
				"Occupation Diagram",
				true);
			// Hidden until RenderExportDialogState::open flips true (toolbar "Export PNG..." or
			// the F12 command) - a real dockable panel, not a modal, since modal popups need a
			// live ImGui window's ID stack at the OpenPopup() call site, which the F12 command
			// path (an input-event handler) doesn't have. Registered after m_ElectronicStructureSession
			// (needs it for the orbital overlay section).
			m_ExportImagePanelId = registerPanel<ExportImagePanel>(
				*rendererLayer,
				m_ElectronicStructureSession,
				m_EventBus,
				"Export Image",
				false);

			if (auto commandRegistry = m_CommandRegistry.lock())
			{
				auto result = commandRegistry->Register(
					CommandMeta{
						CommandID{"electronic_structure.load_focused"},
						"Electronic Structure: Load for focused window",
						"Renderer",
						"Load electronic structure (band gap + orbitals) for the currently focused "
						"renderer window.",
						{},
						CommandFlags::None},
					[session = m_ElectronicStructureSession](CommandContext &) -> Unique<ICommand> {
						return CreateUnique<LoadElectronicStructureForFocusedWindowCommand>(session);
					});
				if (!result)
					DS_LOG_WARN("Electronic structure command registration failed: {}", result.Error().technicalDetails);
			}
		}
		m_SettingsPanelId = registerPanel<SettingsPanel>(
			m_EventBus,
			m_JobSystem,
			CreateWeakRef(m_UiState),
			m_KeymapResolver,
			m_CommandRegistry,
			m_CurrentConfig != nullptr ? *m_CurrentConfig : ApplicationConfig{},
			"Settings",
			true,
			m_RendererLayer);
		m_PanelsInitialized = true;
		applyPanelVisibilityState();

		loadAndQueueProjectWindowRestores();
	}

	void EditorLayer::saveProjectWindowState()
	{
		auto rendererLayer = m_RendererLayer.lock();
		if (rendererLayer == nullptr)
			return;

		std::ostringstream stream;
		for (const RendererWindowState &windowState : rendererLayer->GetWindows())
		{
			if (windowState.structure.sourcePath.Empty())
				continue;

			PersistedWindowRecord record;
			record.sourcePath = windowState.structure.sourcePath;
			record.displayName = windowState.title;
			if (std::optional<RendererViewSnapshot> snapshot =
					rendererLayer->CaptureWindowViewSnapshot(windowState.windowId))
				record.view = *snapshot;
			record.showAtoms = windowState.showAtoms;
			record.showBonds = windowState.showBonds;
			record.showCellBox = windowState.showCellBox;
			record.showGrid = windowState.showGrid;
			record.orbitalUpEnabled = windowState.orbitalChannelUp.enabled;
			record.orbitalUpPositiveColor = windowState.orbitalChannelUp.positiveLobeColor;
			record.orbitalUpNegativeColor = windowState.orbitalChannelUp.negativeLobeColor;
			record.orbitalUpAlpha = windowState.orbitalChannelUp.lobeAlpha;
			record.orbitalDownEnabled = windowState.orbitalChannelDown.enabled;
			record.orbitalDownPositiveColor = windowState.orbitalChannelDown.positiveLobeColor;
			record.orbitalDownNegativeColor = windowState.orbitalChannelDown.negativeLobeColor;
			record.orbitalDownAlpha = windowState.orbitalChannelDown.lobeAlpha;

			if (m_ElectronicStructureSession != nullptr)
			{
				if (const ElectronicStructureSession::WindowState *esState =
						m_ElectronicStructureSession->FindWindowState(windowState.windowId))
				{
					record.hasElectronicStructureState = true;
					record.esBandStart = esState->bandStart;
					record.esBandEnd = esState->bandEnd;
					record.esGapWindowMargin = esState->gapWindowMargin;
					record.esLocalizationThreshold = esState->localizationThreshold;
					record.esSplitSpinChannels = esState->splitSpinChannels;
					record.esRelativeToVbm = esState->relativeToVbm;
					record.esSelectedBand = esState->selectedBand;
					record.esIsoValue = esState->isoValue;
					for (const auto &[key, value] : esState->isoValueByKey)
					{
						const int spinChannel = static_cast<int>(key >> 32);
						const int band = static_cast<int>(key & 0xffffffffu);
						record.esIsoOverrides.emplace_back(spinChannel, band, value);
					}
				}
			}

			stream << SerializeWindowRecord(record);
		}

		std::string error;
		if (!TextFileIO::Save(ProjectWindowsStatePath(), stream.str(), error))
			DS_LOG_WARN("EditorLayer: failed to persist project window state: {}", error);
	}

	void EditorLayer::loadAndQueueProjectWindowRestores()
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(ProjectWindowsStatePath(), text, error))
			return;

		for (PersistedWindowRecord &record : ParseProjectWindowsState(text))
		{
			if (record.sourcePath.Empty() || !FileSystem::Exists(record.sourcePath.Native()))
				continue;

			const std::string predictedId = ComputeDeterministicRendererWindowId(record.sourcePath);
			m_PendingWindowRestores[predictedId] = record;

			if (m_EventBus != nullptr)
			{
				RendererEvents::Windows::OpenStructureRequested request;
				request.filePath = record.sourcePath;
				request.displayName = record.displayName;
				m_EventBus->Queue(request);
			}
		}
	}

	void EditorLayer::pollPendingWindowRestores()
	{
		if (m_PendingWindowRestores.empty())
			return;

		auto rendererLayer = m_RendererLayer.lock();
		if (rendererLayer == nullptr)
			return;

		for (RendererWindowState &windowState : rendererLayer->GetWindows())
		{
			const auto it = m_PendingWindowRestores.find(windowState.windowId);
			if (it == m_PendingWindowRestores.end())
				continue;

			const PersistedWindowRecord &record = it->second;
			rendererLayer->ApplyWindowViewSnapshot(windowState.windowId, record.view);
			windowState.showAtoms = record.showAtoms;
			windowState.showBonds = record.showBonds;
			windowState.showCellBox = record.showCellBox;
			windowState.showGrid = record.showGrid;
			windowState.orbitalChannelUp.enabled = record.orbitalUpEnabled;
			windowState.orbitalChannelUp.positiveLobeColor = record.orbitalUpPositiveColor;
			windowState.orbitalChannelUp.negativeLobeColor = record.orbitalUpNegativeColor;
			windowState.orbitalChannelUp.lobeAlpha = record.orbitalUpAlpha;
			windowState.orbitalChannelDown.enabled = record.orbitalDownEnabled;
			windowState.orbitalChannelDown.positiveLobeColor = record.orbitalDownPositiveColor;
			windowState.orbitalChannelDown.negativeLobeColor = record.orbitalDownNegativeColor;
			windowState.orbitalChannelDown.lobeAlpha = record.orbitalDownAlpha;

			if (record.hasElectronicStructureState && m_ElectronicStructureSession != nullptr)
			{
				ElectronicStructureSession::WindowState &esState = m_ElectronicStructureSession->Update(windowState);
				esState.bandStart = record.esBandStart;
				esState.bandEnd = record.esBandEnd;
				esState.gapWindowMargin = record.esGapWindowMargin;
				esState.localizationThreshold = record.esLocalizationThreshold;
				esState.splitSpinChannels = record.esSplitSpinChannels;
				esState.relativeToVbm = record.esRelativeToVbm;
				esState.selectedBand = record.esSelectedBand;
				esState.isoValue = record.esIsoValue;
				for (const auto &[spinChannel, band, value] : record.esIsoOverrides)
					esState.isoValueByKey[ElectronicStructureSession::PackGridKey(spinChannel, band)] = value;

				m_ElectronicStructureSession->DispatchOutputLoad(esState);

				if (windowState.orbitalChannelUp.enabled)
					m_ElectronicStructureSession->EnsureChannelRendered(esState, windowState, 0, 0);
				if (windowState.orbitalChannelDown.enabled)
					m_ElectronicStructureSession->EnsureChannelRendered(esState, windowState, 1, 1);
			}

			m_PendingWindowRestores.erase(it);
		}
	}

	void EditorLayer::savePanelVisibilityState()
	{
		std::ostringstream stream;
		for (const Entry &entry : m_Panels.Entries())
		{
			if (entry.panel == nullptr)
				continue;
			stream << entry.panel->GetTitle() << '=' << (entry.panel->IsVisible() ? '1' : '0') << '\n';
		}

		std::string error;
		if (!TextFileIO::Save(PanelVisibilityStatePath(), stream.str(), error))
			DS_LOG_WARN("EditorLayer: failed to persist panel visibility state: {}", error);
	}

	void EditorLayer::applyPanelVisibilityState()
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(PanelVisibilityStatePath(), text, error))
			return;

		std::unordered_map<std::string, bool> visibilityByTitle;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			const std::size_t separator = line.find('=');
			if (separator == std::string::npos)
				continue;
			visibilityByTitle[line.substr(0, separator)] = line.substr(separator + 1) == "1";
		}

		for (Entry &entry : m_Panels.Entries())
		{
			if (entry.panel == nullptr)
				continue;
			const auto it = visibilityByTitle.find(entry.panel->GetTitle());
			if (it != visibilityByTitle.end())
				entry.panel->SetVisible(it->second);
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
		// project.save (Ctrl+S, CoreLayer::registerSystemCommands) previously only reached
		// ApplicationEventController::onProjectSaveRequested, which just logs - Ctrl+S looked like it
		// worked but silently did nothing, while File>Zapisz bypassed the whole command/event path and
		// called persistCurrentRoots() directly. Routing the same event here makes Ctrl+S do the real
		// save too, instead of deleting the shortcut or leaving it dead.
		AddSubscription(subscribeEditorLayer<CoreEvents::ProjectSaveRequested>(
			*m_EventBus, *this, &EditorLayer::onProjectSaveRequested));
		DS_LOG_INFO("EditorLayer config event handlers bound");
	}

	void EditorLayer::bindProjectRootEvents()
	{
		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("EditorLayer project-root event binding skipped: EventBus unavailable");
			return;
		}

		AddSubscription(subscribeEditorLayer<ProjectEvents::RootAddRequested>(
			*m_EventBus, *this, &EditorLayer::onRootAddRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::RootRemoveRequested>(
			*m_EventBus, *this, &EditorLayer::onRootRemoveRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::RootPathChangedRequested>(
			*m_EventBus, *this, &EditorLayer::onRootPathChangedRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::BulkDirectoryChangeRequested>(
			*m_EventBus, *this, &EditorLayer::onBulkDirectoryChangeRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<RendererEvents::Viewport::WavecarDropped>(
			*m_EventBus, *this, &EditorLayer::onWavecarDropped, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::TextFileOpenRequested>(
			*m_EventBus, *this, &EditorLayer::onTextFileOpenRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::CalculationSummaryOpenRequested>(
			*m_EventBus, *this, &EditorLayer::onCalculationSummaryOpenRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::IrrepLabelOverridesChanged>(
			*m_EventBus, *this, &EditorLayer::onIrrepLabelOverridesChanged, EventPriority::Normal));
	}

	void EditorLayer::loadInitialProjectState()
	{
		std::vector<RecentProjectEntry> recents;
		std::string recentsError;
		(void)RecentProjectsIO::Load(RecentProjectsIO::DefaultFilePath(), recents, recentsError);

		bool opened = false;
		if (!recents.empty())
		{
			ProjectManifest manifest;
			std::string manifestError;
			if (ProjectManifestIO::Load(recents.front().projectDirectory, manifest, manifestError))
			{
				m_ActiveProject = std::move(manifest);
				m_ActiveProjectDirectory = recents.front().projectDirectory;
				opened = true;
			}
			else
			{
				DS_LOG_WARN("EditorLayer: most recent project at '{}' failed to load: {}",
					recents.front().projectDirectory.String(), manifestError);
			}
		}

		if (!opened)
		{
			std::string error;
			if (!ProjectRootsIO::Load(ProjectRootsIO::DefaultFilePath(), m_AdHocRoots, error))
			{
				DS_LOG_WARN("EditorLayer: failed to load project_roots.yaml: {}", error);
				m_AdHocRoots.clear();
			}
			else if (!error.empty())
			{
				// Load() still returns true after a successful migration even if persisting the
				// migrated seed failed (session stays usable) - surface that failure here instead
				// of silently dropping it.
				DS_LOG_WARN("EditorLayer: {}", error);
			}
		}

		refreshProjectDependentPanels();
	}

	void EditorLayer::createNewProject(const Path &directory)
	{
		if (directory.Empty())
			return;

		if (FileSystem::Exists(ProjectManifestIO::ManifestPath(directory).Native()))
		{
			DS_LOG_WARN(
				"EditorLayer: refusing to create a new project at '{}' - manifest.yaml already exists there (use Open instead)",
				directory.String());
			return;
		}

		ProjectManifest manifest = ProjectManifestIO::CreateNew(directory.filename().String());
		std::string error;
		if (!ProjectManifestIO::Save(directory, manifest, error))
		{
			DS_LOG_WARN("EditorLayer: failed to create project at '{}': {}", directory.String(), error);
			return;
		}

		m_ActiveProject = std::move(manifest);
		m_ActiveProjectDirectory = directory;
		refreshProjectDependentPanels();
		touchAndSaveRecentProject(directory);

		if (m_ElectronicStructureSession != nullptr)
		{
			m_ElectronicStructureSession->SetBulkDirectory({});
			m_ElectronicStructureSession->SetIrrepLabelOverrides({});
		}
	}

	void EditorLayer::openProject(const Path &directory)
	{
		if (directory.Empty())
			return;

		ProjectManifest manifest;
		std::string error;
		if (!ProjectManifestIO::Load(directory, manifest, error))
		{
			DS_LOG_WARN("EditorLayer: failed to open project at '{}': {}", directory.String(), error);
			return;
		}

		m_ActiveProject = std::move(manifest);
		m_ActiveProjectDirectory = directory;
		refreshProjectDependentPanels();
		touchAndSaveRecentProject(directory);

		if (m_ElectronicStructureSession != nullptr)
		{
			m_ElectronicStructureSession->SetBulkDirectory(m_ActiveProject->bulkDirectory);
			if (!m_ActiveProject->bulkDirectory.Empty())
				m_ElectronicStructureSession->DispatchBulkLoad();
			m_ElectronicStructureSession->SetIrrepLabelOverrides(m_ActiveProject->irrepLabelOverrides);
		}
	}

	void EditorLayer::touchAndSaveRecentProject(const Path &directory)
	{
		std::vector<RecentProjectEntry> recents;
		std::string error;
		(void)RecentProjectsIO::Load(RecentProjectsIO::DefaultFilePath(), recents, error);
		RecentProjectsIO::Touch(recents, directory);
		if (!RecentProjectsIO::Save(RecentProjectsIO::DefaultFilePath(), recents, error))
			DS_LOG_WARN("EditorLayer: failed to persist recent_projects.yaml: {}", error);
	}

	void EditorLayer::refreshProjectDependentPanels()
	{
		if (auto panel = findPanel(m_ProjectTreePanelId).lock())
		{
			if (auto *treePanel = dynamic_cast<ProjectTreePanel *>(panel.get()))
				treePanel->SetRoots(currentRootsMutable());
		}

		if (auto panel = findPanel(m_CalculatorConsolePanelId).lock())
		{
			if (auto *console = dynamic_cast<CalculatorConsolePanel *>(panel.get()))
			{
				const std::string projectRoot = m_ActiveProject.has_value() ? m_ActiveProjectDirectory.String() : std::string();
				std::vector<std::string> roots;
				for (const ProjectRootEntry &entry : currentRootsMutable())
					roots.push_back(entry.path.String());
				console->SetProjectContext(projectRoot, std::move(roots));
			}
		}
	}

	std::vector<ProjectRootEntry> &EditorLayer::currentRootsMutable()
	{
		if (m_ActiveProject.has_value())
			return m_ActiveProject->roots;
		return m_AdHocRoots;
	}

	void EditorLayer::persistCurrentRoots()
	{
		std::string error;
		if (m_ActiveProject.has_value())
		{
			if (!ProjectManifestIO::Save(m_ActiveProjectDirectory, *m_ActiveProject, error))
				DS_LOG_WARN("EditorLayer: failed to save manifest.yaml: {}", error);
		}
		else if (!ProjectRootsIO::Save(ProjectRootsIO::DefaultFilePath(), m_AdHocRoots, error))
		{
			DS_LOG_WARN("EditorLayer: failed to persist project_roots.yaml: {}", error);
		}
	}

	void EditorLayer::onRootAddRequested(const ProjectEvents::RootAddRequested &event)
	{
		ProjectRootEntry entry;
		entry.id = ProjectRootsIO::GenerateRootId();
		entry.path = event.path;
		entry.label = event.label;
		currentRootsMutable().push_back(std::move(entry));

		refreshProjectDependentPanels();
		persistCurrentRoots();
	}

	void EditorLayer::onRootRemoveRequested(const ProjectEvents::RootRemoveRequested &event)
	{
		std::erase_if(currentRootsMutable(), [&event](const ProjectRootEntry &entry) { return entry.id == event.rootId; });

		refreshProjectDependentPanels();
		persistCurrentRoots();
	}

	void EditorLayer::onRootPathChangedRequested(const ProjectEvents::RootPathChangedRequested &event)
	{
		std::vector<ProjectRootEntry> &roots = currentRootsMutable();
		const auto it = std::find_if(
			roots.begin(), roots.end(), [&event](const ProjectRootEntry &entry) { return entry.id == event.rootId; });
		if (it == roots.end())
			return;
		it->path = event.newPath;

		refreshProjectDependentPanels();
		persistCurrentRoots();
	}

	void EditorLayer::onBulkDirectoryChangeRequested(const ProjectEvents::BulkDirectoryChangeRequested &event)
	{
		if (m_ElectronicStructureSession != nullptr)
		{
			m_ElectronicStructureSession->SetBulkDirectory(event.directory);
			m_ElectronicStructureSession->DispatchBulkLoad();
		}

		if (m_ActiveProject.has_value())
		{
			m_ActiveProject->bulkDirectory = event.directory;
			std::string error;
			if (!ProjectManifestIO::Save(m_ActiveProjectDirectory, *m_ActiveProject, error))
				DS_LOG_WARN("EditorLayer: failed to save manifest.yaml after bulk reference change: {}", error);
		}
	}

	void EditorLayer::onIrrepLabelOverridesChanged(const ProjectEvents::IrrepLabelOverridesChanged &event)
	{
		if (m_ElectronicStructureSession != nullptr)
			m_ElectronicStructureSession->SetIrrepLabelOverrides(event.overrides);

		if (m_ActiveProject.has_value())
		{
			m_ActiveProject->irrepLabelOverrides = event.overrides;
			std::string error;
			if (!ProjectManifestIO::Save(m_ActiveProjectDirectory, *m_ActiveProject, error))
				DS_LOG_WARN("EditorLayer: failed to save manifest.yaml after irrep label change: {}", error);
		}
	}

	void EditorLayer::onTextFileOpenRequested(const ProjectEvents::TextFileOpenRequested &event)
	{
		if (auto panel = findPanel(m_TextEditorPanelId).lock())
		{
			if (auto *textEditor = dynamic_cast<TextEditorPanel *>(panel.get()))
				textEditor->OpenFile(event.path);
		}
	}

	void EditorLayer::onCalculationSummaryOpenRequested(const ProjectEvents::CalculationSummaryOpenRequested &event)
	{
		if (auto panel = findPanel(m_CalculationSummaryPanelId).lock())
		{
			if (auto *summary = dynamic_cast<CalculationSummaryPanel *>(panel.get()))
				summary->OpenDirectory(event.directory);
		}
	}

	void EditorLayer::onWavecarDropped(const RendererEvents::Viewport::WavecarDropped &event)
	{
		if (m_ElectronicStructureSession == nullptr)
			return;

		auto rendererLayer = m_RendererLayer.lock();
		if (rendererLayer == nullptr)
			return;

		for (RendererWindowState &windowState : rendererLayer->GetWindows())
		{
			if (windowState.windowId != event.windowId)
				continue;

			ElectronicStructureSession::WindowState &state = m_ElectronicStructureSession->Update(windowState);
			state.calculationDirectory = event.wavecarPath.parent_path();
			m_ElectronicStructureSession->DispatchOutputLoad(state);
			DS_LOG_INFO("WAVECAR dropped on window '{}': calculationDirectory set to '{}'",
				event.windowId, state.calculationDirectory.String());
			return;
		}
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

	void EditorLayer::onProjectSaveRequested(const CoreEvents::ProjectSaveRequested &)
	{
		persistCurrentRoots();
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
		renderToolsMenu();
		renderHelpMenu();

		ImGui::EndMainMenuBar();
	}

	void EditorLayer::renderFileMenu()
	{
		if (!ImGui::BeginMenu("Plik"))
			return;

		if (ImGui::MenuItem("Nowy"))
		{
			Result<std::optional<Path>> picked = Platform::PickFolder({});
			if (picked && picked->has_value())
				createNewProject(picked->value());
		}
		if (ImGui::MenuItem("Otworz"))
		{
			Result<std::optional<Path>> picked = Platform::PickFolder({});
			if (picked && picked->has_value())
				openProject(picked->value());
		}
		if (ImGui::MenuItem("Zapisz", "Ctrl+S"))
			persistCurrentRoots();
		if (ImGui::BeginMenu("Ostatnie projekty"))
		{
			std::vector<RecentProjectEntry> recents;
			std::string recentsError;
			(void)RecentProjectsIO::Load(RecentProjectsIO::DefaultFilePath(), recents, recentsError);
			if (recents.empty())
				ImGui::MenuItem("Brak ostatnich projektow", nullptr, false, false);
			else
			{
				for (const RecentProjectEntry &entry : recents)
				{
					if (ImGui::MenuItem(entry.projectDirectory.String().c_str()))
						openProject(entry.projectDirectory);
				}
			}
			ImGui::EndMenu();
		}
		// Real viewport PNG export (ExportImagePanel/renderer.export.image, F12) already exists -
		// points at it instead of the JSON/CSV/LaTeX/ZIP placeholders that had no feature behind them.
		if (ImGui::MenuItem("Eksport obrazu (PNG)..."))
		{
			if (auto panel = findPanel(m_ExportImagePanelId).lock())
			{
				panel->SetVisible(true);
				ImGui::SetWindowFocus(panel->GetTitle().c_str());
			}
		}
		if (ImGui::MenuItem("Wyjdz", "Ctrl+Shift+W"))
		{
			if (auto commandRegistry = m_CommandRegistry.lock())
			{
				CommandContext context;
				Result<CommandOutcome> result = commandRegistry->Execute(CommandID{"app.quit"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Quit command failed: {}", result.Error().technicalDetails);
			}
		}
		ImGui::EndMenu();
	}

	void EditorLayer::renderEditMenu()
	{
		if (!ImGui::BeginMenu("Edycja"))
			return;

		auto executeCommand = [this](const char *commandId)
		{
			if (auto commandRegistry = m_CommandRegistry.lock())
			{
				CommandContext context;
				Result<CommandOutcome> result = commandRegistry->Execute(CommandID{commandId}, std::move(context));
				if (!result)
					DS_LOG_WARN("Command '{}' failed: {}", commandId, result.Error().technicalDetails);
			}
		};

		// edit.undo/edit.redo already exist and work (Ctrl+Z/Ctrl+Y, CoreLayer::registerSystemCommands)
		// - these were disabled stubs pretending the feature didn't exist.
		if (ImGui::MenuItem("Cofnij", "Ctrl+Z"))
			executeCommand("edit.undo");
		if (ImGui::MenuItem("Ponow", "Ctrl+Y"))
			executeCommand("edit.redo");
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

		ImGui::EndMenu();
	}

	void EditorLayer::renderToolsMenu()
	{
		if (!ImGui::BeginMenu("Narzedzia"))
			return;

		// The Settings panel already exists (registered visible-by-default) - this just gives it a
		// conventional Tools>Preferences entry point instead of leaving it reachable only from Widok.
		if (ImGui::MenuItem("Preferencje"))
		{
			if (auto panel = findPanel(m_SettingsPanelId).lock())
			{
				panel->SetVisible(true);
				ImGui::SetWindowFocus(panel->GetTitle().c_str());
			}
		}
		ImGui::EndMenu();
	}

	void EditorLayer::renderHelpMenu()
	{
		if (!ImGui::BeginMenu("Pomoc"))
			return;

		// The keybindings table (chord/command/description/context) already exists inside Settings -
		// points at it rather than duplicating it in a second window.
		if (ImGui::MenuItem("Lista skrotow"))
		{
			if (auto panel = findPanel(m_SettingsPanelId).lock())
			{
				panel->SetVisible(true);
				ImGui::SetWindowFocus(panel->GetTitle().c_str());
			}
		}
		ImGui::EndMenu();
	}

} // namespace DefectStudio
