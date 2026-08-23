#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Layer.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "IO/ProjectManifestIO.hpp"
#include "Presentation/Panels/ElectronicStructureSession.hpp"
#include "Presentation/Panels/PanelRegistry.hpp"
#include "Presentation/EditorUiState.hpp"
#include "Presentation/Panels/LoggingPanel.hpp"
#include "Presentation/Panels/ProgressMonitorWindow.hpp"
#include "Presentation/Panels/ProjectTreePanel.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"
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
	class RendererLayer;
	class DomainLayer;
	struct CommandID;
	struct ApplicationConfig;

	// One open renderer window's full state, as persisted to/restored from
	// install/users/default/config/project_windows.txt (see EditorLayer::saveProjectWindowState /
	// loadAndQueueProjectWindowRestores). Keyed by source file at rest (that's the one thing that
	// survives a restart - windowId is deterministic but only knowable once the hash formula is
	// applied, see RendererStartupBootstrap::GenerateRendererWindowId).
	struct PersistedWindowRecord
	{
		Path sourcePath;
		std::string displayName;
		RendererViewSnapshot view;
		bool showAtoms = true;
		bool showBonds = true;
		bool showCellBox = true;
		bool showGrid = true;
		bool orbitalUpEnabled = false;
		glm::vec3 orbitalUpPositiveColor{1.0f, 0.0f, 0.0f};
		glm::vec3 orbitalUpNegativeColor{0.0f, 0.0f, 1.0f};
		float orbitalUpAlpha = 0.6f;
		bool orbitalDownEnabled = false;
		glm::vec3 orbitalDownPositiveColor{1.0f, 0.0f, 0.0f};
		glm::vec3 orbitalDownNegativeColor{0.0f, 0.0f, 1.0f};
		float orbitalDownAlpha = 0.6f;
		// Electronic structure (ElectronicStructureSession::WindowState) - only meaningful if
		// hasElectronicStructureState is true (a window the user never opened the ES panel for
		// while focused on has no ES session state to restore).
		bool hasElectronicStructureState = false;
		int esBandStart = 1022;
		int esBandEnd = 1027;
		int esGapWindowMargin = 10;
		float esLocalizationThreshold = 0.0f;
		bool esSplitSpinChannels = true;
		bool esRelativeToVbm = false;
		int esSelectedBand = -1;
		float esIsoValue = 0.03f;
		// (spinChannel, band, isoValue) - only the orbitals whose iso value was individually tuned.
		std::vector<std::tuple<int, int, float>> esIsoOverrides;
	};

	namespace CoreEvents
	{
		struct OpenCommandPaletteRequested;
		struct ProjectSaveRequested;
	}

	namespace AppEvents::Config
	{
		struct Applied;
	}

	namespace ProjectEvents
	{
		struct RootAddRequested;
		struct RootRemoveRequested;
		struct RootPathChangedRequested;
		struct BulkDirectoryChangeRequested;
		struct TextFileOpenRequested;
	}

	namespace RendererEvents::Viewport
	{
		struct WavecarDropped;
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
		                         WeakRef<CommandRegistry> commandRegistry,
		                         WeakRef<RendererLayer> rendererLayer,
		                         AtomStyleTable atomStyleTable = {},
		                         Path atomStylesPath = {},
		                         WeakRef<DomainLayer> domainLayer = {},
		                         ElementPropertiesTable elementPropertiesTable = {});
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
		void renderToolsMenu();
		void renderHelpMenu();
		void initializePanelsIfNeeded();
		void handleFontShortcuts(Event &event);
		void renderCommandPalettePopup();
		void executeCommandFromPalette(const CommandID &id);
		void bindConfigEvents();
		void applyConfigToUiState(const ApplicationConfig &config);
		void onConfigApplied(const AppEvents::Config::Applied &event);
		void onOpenCommandPaletteRequested(const CoreEvents::OpenCommandPaletteRequested &event);
		void onProjectSaveRequested(const CoreEvents::ProjectSaveRequested &event);

		// T07.5.1/T07.5.4/T07.5.5 project system - a project is a user-chosen directory holding
		// manifest.yaml (ProjectManifestIO), distinct from the data `roots` it references. With no
		// project open, roots fall back to the ad-hoc ProjectRootsIO placeholder (session-durable,
		// not a named project) - see docs/work/project/TODO.md T07.5.1 for what's still out of
		// scope (PathResolver, tags, migration pipeline, canonical/recovery save split, ...).
		void bindProjectRootEvents();
		void loadInitialProjectState();
		void createNewProject(const Path &directory);
		void openProject(const Path &directory);
		void touchAndSaveRecentProject(const Path &directory);
		// Pushes whichever root list is currently authoritative (active project's manifest, or
		// the ad-hoc list) into the single ProjectTreePanel instance.
		void refreshProjectTreePanel();
		[[nodiscard]] std::vector<ProjectRootEntry> &currentRootsMutable();
		void persistCurrentRoots();
		void onRootAddRequested(const ProjectEvents::RootAddRequested &event);
		void onRootRemoveRequested(const ProjectEvents::RootRemoveRequested &event);
		void onRootPathChangedRequested(const ProjectEvents::RootPathChangedRequested &event);
		void onBulkDirectoryChangeRequested(const ProjectEvents::BulkDirectoryChangeRequested &event);
		// T08.6.4: WAVECAR dragged from ProjectTreePanel onto an open structure's viewport.
		void onWavecarDropped(const RendererEvents::Viewport::WavecarDropped &event);
		// Text editor panel: double-click on any project-tree leaf file.
		void onTextFileOpenRequested(const ProjectEvents::TextFileOpenRequested &event);

		// Minimal stand-in for real T07.5.1 project persistence - see PersistedWindowRecord.
		// Save happens once, at shutdown (OnDetach) - the alternative (a save call at every one of
		// the dozens of mutation sites across RendererLayer/ElectronicStructureSession) is real
		// wiring risk for marginal benefit over "capture the whole world once, reliably, on exit".
		// Load happens once at startup: publish an OpenStructureRequested per persisted window
		// (same event ProjectTreePanel's RMB "Open Defect" uses - no new opening mechanism) and
		// remember what to apply once each one actually appears; pollPendingWindowRestores (called
		// every frame from OnImGuiRender) applies+forgets each entry as its window shows up, and
		// costs nothing once the pending map is empty.
		void saveProjectWindowState();
		void loadAndQueueProjectWindowRestores();
		void pollPendingWindowRestores();

		// Panel open/closed state (IPanel::IsVisible) - a documented-but-unbuilt gap (ADR-008 calls
		// out "panel visibility" as part of workspace/UI autosave). Same shape as the project window
		// state above: save once at shutdown, apply once right after every panel is registered at
		// startup - simpler and safer than threading this through ApplicationConfig/
		// YamlConfigSerializer, which already has several duplicated read/write paths for the ui:
		// section and no existing per-panel concept. Keyed by title (registerPanel's titles are
		// fixed string literals, not user-renamable), so a panel added/removed between versions just
		// leaves an unused/missing line rather than breaking anything.
		void savePanelVisibilityState();
		void applyPanelVisibilityState();

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
		WeakRef<RendererLayer> m_RendererLayer;
		AtomStyleTable m_AtomStyleTable;
		Path m_AtomStylesPath;
		WeakRef<DomainLayer> m_DomainLayer;
		ElementPropertiesTable m_ElementPropertiesTable;
		Ref<EditorUiState> m_UiState;
		Ref<ApplicationConfig> m_CurrentConfig;
		bool m_CommandPaletteOpenRequested = false;
		int m_CommandPaletteSelection = 0;
		std::array<char, 128> m_CommandPaletteSearchBuffer{};
		Ref<ElectronicStructureSession> m_ElectronicStructureSession;
		// Keyed by the predicted deterministic windowId (hash of sourcePath) - see
		// RendererStartupBootstrap::GenerateRendererWindowId, which pollPendingWindowRestores
		// recomputes to match against whatever actually shows up in RendererLayer::GetWindows().
		std::unordered_map<std::string, PersistedWindowRecord> m_PendingWindowRestores;

		// Project system state (see the method-block comment above). Exactly one of these is the
		// "current" root list at any time: m_ActiveProject's roots if a project is open, else
		// m_AdHocRoots. Kept in sync with disk on every mutation (not batched to shutdown like
		// m_PendingWindowRestores above - root list changes are rare and shouldn't risk being lost
		// to a crash).
		std::optional<ProjectManifest> m_ActiveProject;
		Path m_ActiveProjectDirectory;
		std::vector<ProjectRootEntry> m_AdHocRoots;
		PanelId m_ProjectTreePanelId = 0;
		PanelId m_TextEditorPanelId = 0;
		PanelId m_SettingsPanelId = 0;
		PanelId m_ExportImagePanelId = 0;
	};

	template <typename TPanel, typename... Args>
	PanelId EditorLayer::registerPanel(Args &&...args)
	{
		return m_Panels.Add<TPanel>(std::forward<Args>(args)...);
	}
} // namespace DefectStudio
