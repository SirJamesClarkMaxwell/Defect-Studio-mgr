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
#include "Presentation/Panels/CellPanel.hpp"
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

	// Un-hides the panel if the user had closed it, then queues focus until after panel rendering.
	// A closed docked window has no ImGui window to focus during command execution.
	class FocusPanelCommand final : public ICommand
	{
	public:
		FocusPanelCommand(WeakRef<IPanel> panel, std::function<void(const std::string &)> requestFocus,
		                  std::string description)
			: m_Panel(std::move(panel)), m_RequestFocus(std::move(requestFocus)), m_Description(std::move(description))
		{
		}

		Result<void> Execute(CommandContext &) override
		{
			Ref<IPanel> panel = m_Panel.lock();
			if (panel == nullptr)
				return {};
			panel->SetVisible(true);
			m_RequestFocus(panel->GetTitle());
			return {};
		}

		std::string Description() const override
		{
			return m_Description;
		}

	private:
		WeakRef<IPanel> m_Panel;
		std::function<void(const std::string &)> m_RequestFocus;
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
					return CreateUnique<FocusPanelCommand>(
						findPanel(m_ProjectTreePanelId),
						[this](const std::string &title) { m_PendingPanelFocusTitle = title; },
						"Focus Project Tree");
				});
			if (!result)
				DS_LOG_WARN("Focus Project Tree command registration failed: {}", result.Error().technicalDetails);
		}
		m_TextEditorPanelId = registerPanel<TextEditorPanel>("Text Editor", false);
		m_TerminalPanelId = registerPanel<TerminalPanel>("Terminal", false);
		m_CalculatorConsolePanelId = registerPanel<CalculatorConsolePanel>("Integrated Python Console", false);
		registerCalculationSummaryViews();
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
				"terminal.toggle", "Toggle Terminal", "Show and focus the Terminal, or hide it when already focused.",
				[](TerminalPanel &terminal) {
					ImGuiWindow *terminalWindow = ImGui::FindWindowByName("Terminal###TerminalPanel");
					ImGuiWindow *focusedWindow = ImGui::GetCurrentContext()->NavWindow;
					const bool focused = terminalWindow != nullptr
						&& focusedWindow != nullptr
						&& (focusedWindow == terminalWindow || focusedWindow->RootWindow == terminalWindow);
					if (terminal.IsVisible() && focused)
						terminal.SetVisible(false);
					else
						terminal.RequestFocus();
				});
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
				m_DomainLayer,
				"Renderer",
				true);
			const PanelId sceneOutlinerPanelId = registerPanel<SceneOutlinerPanel>(
				*rendererLayer, m_DomainLayer, m_JobSystem, m_ElementPropertiesTable, m_CommandRegistry,
				"Scene Outliner", true);
			if (auto commandRegistry = m_CommandRegistry.lock())
			{
				auto result = commandRegistry->Register(
					CommandMeta{
						CommandID{"editor.focus_scene_outliner"},
						"Focus Scene Outliner",
						"Editor",
						"Bring the Scene Outliner panel to focus, reopening it first if it was closed.",
						{},
						CommandFlags::None},
					[this, sceneOutlinerPanelId](CommandContext &) -> Unique<ICommand> {
						return CreateUnique<FocusPanelCommand>(
							findPanel(sceneOutlinerPanelId),
							[this](const std::string &title) { m_PendingPanelFocusTitle = title; },
							"Focus Scene Outliner");
					});
				if (!result)
					DS_LOG_WARN("Focus Scene Outliner command registration failed: {}", result.Error().technicalDetails);
			}
			registerPanel<ObjectPropertiesPanel>(
				*rendererLayer, m_CommandRegistry, m_DomainLayer, m_AtomStyleTable, "Object Properties", true);
			registerPanel<CellPanel>(
				*rendererLayer, m_CommandRegistry, m_DomainLayer, m_ElementPropertiesTable, m_AtomStyleTable, "Komórka", false);
			registerPanel<BondSettingsPanel>(
				*rendererLayer, m_CommandRegistry, m_DomainLayer, m_ElementPropertiesTable, "Bond Settings", false);
			m_NewStructureWizardPanelId = registerPanel<NewStructureWizardPanel>(
				*rendererLayer, m_DomainLayer, m_JobSystem, m_ElementPropertiesTable, m_AtomStyleTable,
				m_CreationSessionRegistry, m_EventBus,
				"New Structure", true);
			// Config is normally applied before the panels exist, so the fresh wizard has to be told
			// once here as well as on every later apply.
			pushPseudopotentialStateToWizard(
				m_CurrentConfig != nullptr && !m_CurrentConfig->ui.pseudopotentialDir.Empty());
			m_MaterialsCollectionPanelId = registerPanel<MaterialsCollectionPanel>(
				*rendererLayer, m_DomainLayer, m_ElementPropertiesTable, m_AtomStyleTable,
				Path::FromResolved(FileSystem::CurrentPath() / "install" / "users" / "default" / "materials" / "materials.db"),
				"Materials Collection", false);
			m_StructureHubPanelId = registerPanel<StructureHubPanel>(
				m_CreationSessionRegistry,
				m_EventBus,
				"Structure Hub", true);
			// Hosts the three-pane preview window for every open creation session. Without it the
			// session preview windows are never drawn at all - RendererPanel skips them.
			registerPanel<StructureCreationTabsPanel>(
				*rendererLayer, m_CreationSessionRegistry, m_EventBus, m_ContextManager, m_CommandRegistry,
				m_ElementPropertiesTable, m_AtomStyleTable,
				"Structure Creation", true);
			registerPanel<ElementCatalogPanel>(
				*rendererLayer, m_CommandRegistry, m_AtomStyleTable, m_ElementPropertiesTable, m_AtomStylesPath,
				"Element Catalog", false);
			m_DisplacementComparisonPanelId = registerPanel<DisplacementComparisonPanel>(
				*rendererLayer, m_DomainLayer, m_JobSystem, m_EventBus, m_ElementPropertiesTable,
				"Atoms Displacement", false);
			registerPanel<GroupTheoryPanel>(*rendererLayer, m_DomainLayer, m_JobSystem, "Group Theory", false);
			m_FnvCorrectionPanelId = registerPanel<FnvCorrectionPanel>(*rendererLayer, m_JobSystem, "FNV Correction", false);
			registerPhononViews();
			registerDefectEnergeticsViews();
			// Shared model/job-dispatch behind both panels below (band data, caches, in-flight
			// jobs) - split into two windows so the occupation plot can fill its own window
			// instead of a fixed height squeezed under a long list of controls. Kept as a member
			// (not local) so saveProjectWindowState/pollPendingWindowRestores can reach it too.
			m_ElectronicStructureSession = CreateRef<ElectronicStructureSession>(*rendererLayer, m_JobSystem);
			m_SceneDensityLoader = CreateRef<SceneDensityLoader>(*rendererLayer, m_JobSystem);
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
		// Startup project loading runs before the analysis panels exist.
		loadFnvCorrectionsForProject();
		loadPhononsForProject();
		applyPanelVisibilityState();

		loadAndQueueProjectWindowRestores();
	}

} // namespace DefectStudio
