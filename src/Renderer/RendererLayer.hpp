#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Memory.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/RendererConfig.hpp"
#include "Renderer/RendererMeshData.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	class OpenGlRendererBackend;
	class RendererViewCamera;
	class EventBus;
	struct OrbitalGridData;

	namespace RendererEvents::Config
	{
		struct Applied;
	}

	// "cam-fields|selected-positions|hidden-positions" text encoding of a RendererViewSnapshot -
	// shared by RendererLayer's own persisted-views files and EditorLayer's per-window project
	// state, so there is exactly one format/parser for "a saved camera+selection+visibility view"
	// instead of two independently-evolving ones. Defined in RendererLayer.cpp.
	[[nodiscard]] std::string SerializeViewSnapshot(const RendererViewSnapshot &snapshot);
	[[nodiscard]] std::optional<RendererViewSnapshot> DeserializeViewSnapshot(const std::string &line);

	// Snapshots windowState.pinnedMeasurements onto its undo history and clears the redo history -
	// call once BEFORE any pin mutation (add/remove/flip/drag-start), never per-frame during a drag,
	// so a whole drag/bulk-add/bulk-remove collapses into one undo step (Ctrl+Alt+U/Ctrl+Alt+Shift+U,
	// see RendererEvents::Viewport::UndoLabelsRequested for why this is a separate local stack from
	// the global Ctrl+Z domain undo). Free function, not a RendererLayer member, so both
	// RendererLayer.cpp's internal pin-mutation helpers and RendererPanel (which owns the actual
	// click/drag edits) can call it without routing through a layer method for no reason.
	void PushPinnedMeasurementUndoSnapshot(RendererWindowState &windowState);

	struct RendererStartupConfig
	{
		Path assetsDirectory;
		Path shaderDirectory;
		std::vector<RendererWindowState> startupWindows;
		RendererPrimitiveMeshAssets primitiveMeshes;
		std::vector<std::string> periodicTableSymbols;
		std::vector<std::string> lanthanideSymbols;
		std::vector<std::string> actinideSymbols;
		bool loadDefaultScene = true;
	};

	class RendererLayer final : public Layer, public EventReceiver
	{
	public:
		explicit RendererLayer(RendererStartupConfig startupConfig);
		~RendererLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		void OnImGuiRender() override;
		void ApplyConfig(const RendererConfig &config);
		void BindEventBus(Ref<EventBus> eventBus);
		[[nodiscard]] Ref<EventBus> GetEventBus() const;
		void BeginViewInteraction(const std::string &windowId, std::string sourceAction);
		void CommitViewInteraction(const std::string &windowId);
		void CancelViewInteraction(const std::string &windowId);
		void StartCameraTransition(
			const std::string &windowId,
			const glm::vec3 &target,
			float distance,
			float yaw,
			float pitch,
			float roll,
			const char *sourceAction = nullptr);
		void UpdateCameraTransitions(float deltaTime);
		void UndoViewChange(const std::string &windowId);
		void RedoViewChange(const std::string &windowId);
		// Local per-window undo/redo for pinned measurement labels - see RendererEvents::Viewport::
		// UndoLabelsRequested's comment for why this is its own stack instead of the global Ctrl+Z.
		void UndoLabelsChange(const std::string &windowId);
		void RedoLabelsChange(const std::string &windowId);
		void SetViewportSize(const std::string &windowId, glm::vec2 size);
		// Appends a runtime-opened window (e.g. Project Tree "Open Defect") - main thread only,
		// callers must have already built a fully-formed RendererWindowState (see
		// App/RendererRuntimeOpenCoordinator, which mirrors RendererStartupComposer's construction).
		void AddWindow(RendererWindowState windowState);
		// Closes and discards windowId's window (RendererPanel, on its titlebar X). No-op if
		// unknown.
		void RemoveWindow(const std::string &windowId);
		[[nodiscard]] std::vector<RendererWindowState> &GetWindows();
		[[nodiscard]] const std::vector<RendererWindowState> &GetWindows() const;
		[[nodiscard]] RendererGlobalRenderSettings &GetGlobalSettings();
		[[nodiscard]] const RendererGlobalRenderSettings &GetGlobalSettings() const;
		[[nodiscard]] bool IsAttached() const noexcept;
		[[nodiscard]] const std::string &GetFocusedViewportWindowId() const noexcept;
		// Unlike GetFocusedViewportWindowId(), this stays set when ImGui focus moves to another
		// panel (e.g. clicking a button in ElectronicStructurePanel) - only changes when a
		// DIFFERENT viewport window gains focus. For "which structure is this tool inspecting"
		// UI, not for camera-input focus gating (that's what GetFocusedViewportWindowId is for).
		[[nodiscard]] const std::string &GetLastFocusedViewportWindowId() const noexcept;
		[[nodiscard]] float GetLastDeltaTime() const noexcept;
		[[nodiscard]] const RendererToolbarIconTexture *GetToolbarIcon(const std::string &fileName) const;
		[[nodiscard]] const std::vector<std::string> &GetPeriodicTableSymbols() const;
		[[nodiscard]] const std::vector<std::string> &GetLanthanideSymbols() const;
		[[nodiscard]] const std::vector<std::string> &GetActinideSymbols() const;
		[[nodiscard]] unsigned int RenderToFbo(
			const std::string &windowKey,
			const RendererStructureData &structure,
			const RendererWindowState &windowState,
			const RendererGlobalRenderSettings &settings);
		void CollectProfilingData();
		bool &GetShowPeriodicTableWindow();
		std::string &GetSelectedPeriodicElement();
		RenderExportDialogState &GetExportDialogState();
		// Reads back windowKey's last-rendered frame (via RenderToFbo) and writes it to a PNG.
		// Returns false + fills error on missing viewport or write failure. crop* are fractions
		// (0..1) trimmed from each edge before writing.
		[[nodiscard]] bool CaptureWindowToPng(
			const std::string &windowKey,
			const Path &outputPath,
			std::string &error,
			float cropLeft = 0.0f,
			float cropRight = 0.0f,
			float cropTop = 0.0f,
			float cropBottom = 0.0f) const;
		// Regenerates windowId's orbital isosurface on the GPU (compute shader) from an
		// already-loaded grid and writes the resulting vertex count into its RendererWindowState's
		// orbitalChannelUp/Down (slot 0/1) - cheap enough to call on every slider tick
		// (ElectronicStructurePanel), no Python re-fetch. Returns 0 (and leaves the window's mesh
		// untouched) if windowId is unknown.
		int RegenerateOrbitalIsosurface(
			const std::string &windowId, const OrbitalGridData &grid, float isoValue, int slot = 0);
		// Same as RegenerateOrbitalIsosurface, but writes vertexCount into the caller-supplied
		// channel directly instead of looking a live window up by id - for synthetic windowKeys
		// (ExportImagePanel's "__export_preview__"/"__export_full__") that are never in
		// GetWindows(). windowKey must already have an active viewport (see
		// OpenGlRendererBackend::RegenerateIsosurfaceGpu) - call RenderToFbo for it at least once
		// before this, and again after, so the regenerated mesh actually gets drawn.
		int RegenerateOrbitalIsosurfaceForChannel(
			const std::string &windowKey,
			const OrbitalGridData &grid,
			float isoValue,
			int slot,
			RendererWindowState::OrbitalOverlayChannel &channel);
		// Thin public wrappers around the private capture/restore pair, for EditorLayer's
		// project-state persistence (full window snapshot: camera + selection + visibility,
		// keyed by the now-deterministic windowId - see RendererStartupBootstrap). nullopt/no-op
		// on an unknown windowId.
		[[nodiscard]] std::optional<RendererViewSnapshot> CaptureWindowViewSnapshot(const std::string &windowId) const;
		void ApplyWindowViewSnapshot(const std::string &windowId, const RendererViewSnapshot &snapshot);
		// Management API for the saved-views list (Settings panel: apply/rename/delete/reorder).
		// All mutators persist immediately via savePersistedSharedViews(), same as Shift+V.
		[[nodiscard]] const std::vector<RendererViewSnapshot> &GetSharedSavedViews() const;
		void RenameSharedSavedView(std::size_t index, std::string name);
		void DeleteSharedSavedView(std::size_t index);
		// direction < 0 moves one slot earlier, > 0 moves one slot later; 0 is a no-op.
		void MoveSharedSavedView(std::size_t index, int direction);
		// Applies to windowId, or to the last-focused viewport window if windowId is empty.
		void ApplySharedSavedView(std::size_t index, const std::string &windowId = {});

	private:
		void loadDefaultWindows();
		void bindConfigEvents();
		void onConfigApplied(const RendererEvents::Config::Applied &event);
		void onOrbitDelta(const RendererEvents::Viewport::OrbitDelta &event);
		void onPanDelta(const RendererEvents::Viewport::PanDelta &event);
		void onZoomDelta(const RendererEvents::Viewport::ZoomDelta &event);
		void onViewportFocusChanged(const RendererEvents::Viewport::FocusChanged &event);
		void onAlignToAxisRequested(const RendererEvents::Viewport::AlignToAxisRequested &event);
		void onOrbitDirectionRequested(const RendererEvents::Viewport::OrbitDirectionRequested &event);
		void onOrbitQuarterTurnRequested(const RendererEvents::Viewport::OrbitQuarterTurnRequested &event);
		void onRollDirectionRequested(const RendererEvents::Viewport::RollDirectionRequested &event);
		void onZoomDirectionRequested(const RendererEvents::Viewport::ZoomDirectionRequested &event);
		void onOrbitStepRequested(const RendererEvents::Viewport::OrbitStepRequested &event);
		void onPanDirectionRequested(const RendererEvents::Viewport::PanDirectionRequested &event);
		void onPanStepRequested(const RendererEvents::Viewport::PanStepRequested &event);
		void onRollStepRequested(const RendererEvents::Viewport::RollStepRequested &event);
		void onZoomStepRequested(const RendererEvents::Viewport::ZoomStepRequested &event);
		void onFocusSelectedAtomRequested(const RendererEvents::Viewport::FocusSelectedAtomRequested &event);
		void onUndoViewRequested(const RendererEvents::Viewport::UndoViewRequested &event);
		void onRedoViewRequested(const RendererEvents::Viewport::RedoViewRequested &event);
		void onUndoLabelsRequested(const RendererEvents::Viewport::UndoLabelsRequested &event);
		void onRedoLabelsRequested(const RendererEvents::Viewport::RedoLabelsRequested &event);
		void onSaveCurrentViewRequested(const RendererEvents::Viewport::SaveCurrentViewRequested &event);
		void onCycleSavedViewRequested(const RendererEvents::Viewport::CycleSavedViewRequested &event);
		void onExportImageRequested(const RendererEvents::Viewport::ExportImageRequested &event);
		void onViewTransitionRequested(const RendererEvents::Viewport::ViewTransitionRequested &event);
		void onProjectionToggleRequested(const RendererEvents::Viewport::ProjectionToggleRequested &event);
		void onAtomSelectionRequested(const RendererEvents::Viewport::AtomSelectionRequested &event);
		void onBondSelectionRequested(const RendererEvents::Viewport::BondSelectionRequested &event);
		void onSelectionToolToggleRequested(const RendererEvents::Viewport::SelectionToolToggleRequested &event);
		void onGizmoOperationRequested(const RendererEvents::Viewport::GizmoOperationRequested &event);
		void onLabelsToggleRequested(const RendererEvents::Viewport::LabelsToggleRequested &event);
		void onLabelsToggleSelectedBondRequested(const RendererEvents::Viewport::LabelsToggleSelectedBondRequested &event);
		void onLabelsToggleSelectedAngleRequested(const RendererEvents::Viewport::LabelsToggleSelectedAngleRequested &event);
		void onLabelsRemoveSelectedBondRequested(const RendererEvents::Viewport::LabelsRemoveSelectedBondRequested &event);
		void onLabelsShowAllBondRequested(const RendererEvents::Viewport::LabelsShowAllBondRequested &event);
		void onLabelsRemoveAllBondRequested(const RendererEvents::Viewport::LabelsRemoveAllBondRequested &event);
		void onLabelsRemoveSelectedAngleRequested(const RendererEvents::Viewport::LabelsRemoveSelectedAngleRequested &event);
		void onLabelsShowAllAngleRequested(const RendererEvents::Viewport::LabelsShowAllAngleRequested &event);
		void onLabelsRemoveAllAngleRequested(const RendererEvents::Viewport::LabelsRemoveAllAngleRequested &event);
		void onLabelsToggleBondAlignmentRequested(const RendererEvents::Viewport::LabelsToggleBondAlignmentRequested &event);
		void onRegionSelectionRequested(const RendererEvents::Viewport::RegionSelectionRequested &event);
		void onHideSelectionRequested(const RendererEvents::Viewport::HideSelectionRequested &event);
		void onShowAllRequested(const RendererEvents::Viewport::ShowAllRequested &event);
		void onSelectionInvertRequested(const RendererEvents::Viewport::SelectionInvertRequested &event);
		void onSelectAllRequested(const RendererEvents::Viewport::SelectAllRequested &event);
		void onCursor3DSetPositionRequested(const RendererEvents::Viewport::Cursor3DSetPositionRequested &event);
		void onSetAsDefaultViewRequested(const RendererEvents::Viewport::SetAsDefaultViewRequested &event);
		void onApplyDefaultViewRequested(const RendererEvents::Viewport::ApplyDefaultViewRequested &event);
		[[nodiscard]] RendererWindowState *findWindowById(const std::string &windowId);
		[[nodiscard]] RendererWindowState *findViewportCommandWindow(const std::string &windowId);
		[[nodiscard]] RendererViewSnapshot captureViewSnapshot(const RendererWindowState &windowState) const;
		// Minimal stand-in for real T07.5.1 persistence (same pattern as ProjectTreePanel/
		// ElectronicStructureSession's own small text files) - camera pose only (target/distance/
		// yaw/pitch/roll/projection), not the selection/hidden-atom arrays, which are tied to one
		// specific structure and don't carry meaningfully across a restart or onto another window.
		void loadPersistedViews();
		void savePersistedDefaultView();
		void savePersistedSharedViews();
		void restoreViewSnapshot(
			RendererWindowState &windowState,
			const RendererViewSnapshot &snapshot,
			const char *sourceAction);
		void pushViewChange(
			RendererWindowState &windowState,
			const RendererViewSnapshot &before,
			const RendererViewSnapshot &after,
			const char *sourceAction);
		void applyDefaultProjectionToWindows();
		const RendererToolbarIconTexture *getToolbarIcon(const std::string &iconFileName) const;
		void releaseToolbarIcons();
		Path resolveShaderDirectory() const;

	private:
		RendererStartupConfig m_StartupConfig;
		Ref<EventBus> m_EventBus;
		Unique<OpenGlRendererBackend> m_RendererBackend;
		std::vector<RendererWindowState> m_Windows;
		std::vector<std::string> m_PeriodicTableSymbols;
		std::vector<std::string> m_LanthanideSymbols;
		std::vector<std::string> m_ActinideSymbols;
		RendererGlobalRenderSettings m_GlobalRenderSettings;
		mutable std::unordered_map<std::string, RendererToolbarIconTexture> m_ToolbarIcons;
		std::string m_SelectedPeriodicElement = "C";
		std::string m_FocusedViewportWindowId;
		std::string m_LastFocusedViewportWindowId;
		bool m_ShowPeriodicTableWindow = true;
		RenderExportDialogState m_ExportDialog;
		float m_LastDeltaTime = 0.0f;
		bool m_Attached = false;
		// TODO(T07.5.1): promote to real per-project persistence once project manifests exist.
		// Session-scoped only (does not survive app restart) - see autoApplyDefaultViewOnOpen.
		std::optional<RendererViewSnapshot> m_SessionDefaultView;
		// Shared across every renderer window (not per-window) - a view saved with Shift+V while
		// looking at one window is meant to be reusable on any other open window (e.g. lining up
		// two different defects on the same viewing angle), so the list itself is layer-level.
		std::vector<RendererViewSnapshot> m_SharedSavedViews;
		std::size_t m_ActiveSharedSavedViewIndex = 0;
	};
} // namespace DefectStudio
