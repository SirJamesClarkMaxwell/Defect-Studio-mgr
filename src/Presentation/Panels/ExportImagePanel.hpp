#pragma once

#include "Presentation/Panels/ElectronicStructureSession.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class EventBus;

	// Dockable, resizable "Export Image" panel - filename/path, resolution, visibility toggles,
	// zoom + per-edge crop, and a live preview that fills whatever space the panel is given
	// (aspect-locked to the chosen export resolution, letterboxed rather than stretched).
	// Replaces the old BeginPopupModal version: a normal docked window sidesteps the ImGui
	// popup-ID-stack fragility that a modal has (OpenPopup/BeginPopupModal both hash against
	// "the current window's ID stack at the call site", which broke when triggered from the F12
	// command path - an event handler with no ImGui window context at all). Visibility mirrors
	// RenderExportDialogState::open, which the toolbar button and the F12 command both set.
	class ExportImagePanel final : public IPanel
	{
	public:
		explicit ExportImagePanel(
			RendererLayer &layer,
			Ref<ElectronicStructureSession> session,
			Ref<EventBus> eventBus,
			std::string title = "Export Image",
			bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		// Orbital overlay section (T08.6's export-pipeline extension) - color/alpha controls (bound
		// to dialog.previewState.orbitalChannelUp/Down, whose .enabled flags ARE the "show
		// orbitals" toggles), a live preview of the currently-selected band, and the batch
		// band-selection table. `state` is nullptr if this window has no electronic structure
		// loaded yet at all.
		void renderOrbitalExportControls(ElectronicStructureSession::WindowState *state);
		// One step of the running batch export, called every frame regardless of dialog section -
		// no-ops unless dialog.orbitalBatchExport.running. Processes pendingBands front-to-back, one
		// band per frame once its required grid(s) are ready (see
		// ElectronicStructureSession::TryGetOrDispatchGrid/GridFetchError).
		void stepOrbitalBatchExport(
			ElectronicStructureSession::WindowState &state, RendererWindowState &targetWindow, int exportWidth, int exportHeight);
		// Regenerates dialog.previewState's orbital mesh for `band` at windowKey (whichever channels
		// are currently enabled) and renders windowKey twice - see the isovalue per-window GPU
		// buffer fix this session shipped: RegenerateIsosurfaceGpu no-ops on a windowKey with no
		// prior RenderWindow call for it, so a windowKey used only on-demand (unlike
		// "__export_preview__", which is rendered every frame regardless) needs an established-
		// viewport render pass before the mesh regenerates and another after, or the new mesh never
		// gets drawn. Returns false (nothing captured) if a required channel's grid isn't cached yet
		// - caller should wait for TryGetOrDispatchGrid to finish fetching it.
		bool renderOrbitalBandForCapture(
			const std::string &windowKey,
			int band,
			ElectronicStructureSession::WindowState &state,
			RendererWindowState &targetWindow,
			int exportWidth,
			int exportHeight);

		RendererLayer &m_Layer;
		Ref<ElectronicStructureSession> m_Session;
		Ref<EventBus> m_EventBus;
	};
} // namespace DefectStudio
