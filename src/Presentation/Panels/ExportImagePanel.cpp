#include "Core/dspch.hpp"

#include "Presentation/Panels/ExportImagePanel.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "Core/Logging/Logger.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Core/Utils/Path.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	ExportImagePanel::ExportImagePanel(
		RendererLayer &layer,
		Ref<ElectronicStructureSession> session,
		Ref<EventBus> eventBus,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_Session(std::move(session)),
		  m_EventBus(std::move(eventBus))
	{
	}

	Ref<IPanel> ExportImagePanel::Clone() const
	{
		return CreateRef<ExportImagePanel>(*this);
	}

	RendererGlobalRenderSettings ExportImagePanel::resolveExportSettings() const
	{
		RendererGlobalRenderSettings settings = m_Layer.GetGlobalSettings();
		const RenderExportDialogState &dialog = m_Layer.GetExportDialogState();
		if (dialog.useCustomBackground)
			settings.backgroundColor = dialog.backgroundColor;
		// Export already renders at the dialog's own exact resolution preset/custom size - stacking
		// the live interactive-viewport supersample multiplier (RendererLayer::RenderToFbo) on top
		// would silently inflate the output past what the preset promises (e.g. "1080p" coming out
		// larger). Export has its own separate detail knob for orbitals (orbitalSupersample).
		settings.viewportSupersample = 1.0f;
		return settings;
	}

	bool ExportImagePanel::renderOrbitalBandForCapture(
		const std::string &windowKey,
		int band,
		ElectronicStructureSession::WindowState &state,
		RendererWindowState &targetWindow,
		int exportWidth,
		int exportHeight)
	{
		RenderExportDialogState &dialog = m_Layer.GetExportDialogState();
		const bool wantUp = dialog.previewState.orbitalChannelUp.enabled;
		const bool wantDown = dialog.previewState.orbitalChannelDown.enabled;
		if (!wantUp && !wantDown)
			return true; // nothing to overlay - a plain structure capture is still valid

		const OrbitalGridData *gridUp = wantUp ? m_Session->TryGetOrDispatchGrid(state, 0, band) : nullptr;
		const OrbitalGridData *gridDown = wantDown ? m_Session->TryGetOrDispatchGrid(state, 1, band) : nullptr;
		if ((wantUp && gridUp == nullptr) || (wantDown && gridDown == nullptr))
			return false; // still pending (or just failed - caller checks GridFetchError first)

		// Export-only mesh smoothing (see the "Orbital mesh detail" combo) - upsample a local copy
		// rather than touching the session's cached grid, which the interactive view still reads at
		// native resolution.
		OrbitalGridData upsampledUp;
		OrbitalGridData upsampledDown;
		if (dialog.orbitalSupersample > 1)
		{
			if (gridUp != nullptr)
			{
				upsampledUp = UpsampleOrbitalGrid(*gridUp, dialog.orbitalSupersample);
				gridUp = &upsampledUp;
			}
			if (gridDown != nullptr)
			{
				upsampledDown = UpsampleOrbitalGrid(*gridDown, dialog.orbitalSupersample);
				gridDown = &upsampledDown;
			}
		}

		dialog.previewState.viewportSize = glm::vec2(static_cast<float>(exportWidth), static_cast<float>(exportHeight));
		if (dialog.previewState.camera != nullptr)
			dialog.previewState.camera->SetViewport(static_cast<float>(exportWidth), static_cast<float>(exportHeight));

		// Pass 1: establish/refresh windowKey's viewport (see this method's header comment).
		(void)m_Layer.RenderToFbo(windowKey, targetWindow.structure, dialog.previewState, resolveExportSettings());

		if (wantUp)
		{
			const std::uint64_t key = ElectronicStructureSession::PackGridKey(0, band);
			m_Layer.RegenerateOrbitalIsosurfaceForChannel(
				windowKey, *gridUp, ElectronicStructureSession::ResolveIsoValue(state, key), 0,
				dialog.previewState.orbitalChannelUp);
		}
		if (wantDown)
		{
			const std::uint64_t key = ElectronicStructureSession::PackGridKey(1, band);
			m_Layer.RegenerateOrbitalIsosurfaceForChannel(
				windowKey, *gridDown, ElectronicStructureSession::ResolveIsoValue(state, key), 1,
				dialog.previewState.orbitalChannelDown);
		}

		// Pass 2: now the freshly-regenerated mesh actually gets drawn.
		(void)m_Layer.RenderToFbo(windowKey, targetWindow.structure, dialog.previewState, resolveExportSettings());
		return true;
	}

	void ExportImagePanel::stepOrbitalBatchExport(
		ElectronicStructureSession::WindowState &state, RendererWindowState &targetWindow, int exportWidth, int exportHeight)
	{
		RenderExportDialogState &dialog = m_Layer.GetExportDialogState();
		RenderExportDialogState::OrbitalBatchExportState &batch = dialog.orbitalBatchExport;
		if (!batch.running)
			return;
		if (batch.pendingBands.empty())
		{
			batch.running = false;
			return;
		}

		const int band = batch.pendingBands.front();
		const bool wantUp = dialog.previewState.orbitalChannelUp.enabled;
		const bool wantDown = dialog.previewState.orbitalChannelDown.enabled;
		const bool upFailed = wantUp &&
			ElectronicStructureSession::GridFetchError(state, ElectronicStructureSession::PackGridKey(0, band)) != nullptr;
		const bool downFailed = wantDown &&
			ElectronicStructureSession::GridFetchError(state, ElectronicStructureSession::PackGridKey(1, band)) != nullptr;
		if (upFailed || downFailed)
		{
			DS_LOG_WARN("Orbital batch export: skipping band {} (wavefunction load failed)", band);
			++batch.skippedCount;
			batch.pendingBands.erase(batch.pendingBands.begin());
			return;
		}

		if (!renderOrbitalBandForCapture("__export_full__", band, state, targetWindow, exportWidth, exportHeight))
			return; // still waiting on a grid fetch - try again next frame

		std::string error;
		const std::string safeName = dialog.filename.empty() ? "export" : dialog.filename;
		const Path outputPath = dialog.saveDirectory / (safeName + "_band" + std::to_string(band) + ".png");
		if (!m_Layer.CaptureWindowToPng(
				"__export_full__", outputPath, error, dialog.cropLeft, dialog.cropRight, dialog.cropTop, dialog.cropBottom))
			DS_LOG_ERROR("Orbital batch export: PNG export failed for band {}: {}", band, error);
		else
			DS_LOG_INFO("Orbital batch export: {}", outputPath.String());

		++batch.completedCount;
		batch.pendingBands.erase(batch.pendingBands.begin());
		if (batch.pendingBands.empty())
			batch.running = false;
	}

	void ExportImagePanel::renderOrbitalExportControls(ElectronicStructureSession::WindowState *state)
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Orbital overlay");

		if (state == nullptr || !state->data.has_value() || !state->data->orbitals.has_value())
		{
			ImGui::TextDisabled("No electronic structure loaded for this window - open Electronic Structure panel and click Load.");
			return;
		}

		RenderExportDialogState &dialog = m_Layer.GetExportDialogState();

		bool upEnabled = dialog.previewState.orbitalChannelUp.enabled;
		if (ImGui::Checkbox("Show spin up##export", &upEnabled))
			dialog.previewState.orbitalChannelUp.enabled = upEnabled;
		ImGui::SameLine();
		bool downEnabled = dialog.previewState.orbitalChannelDown.enabled;
		if (ImGui::Checkbox("Show spin down##export", &downEnabled))
			dialog.previewState.orbitalChannelDown.enabled = downEnabled;

		if (!upEnabled && !downEnabled)
			return;

		static const char *supersampleLabels[] = {"1x (native)", "2x", "3x"};
		int supersampleIndex = std::clamp(dialog.orbitalSupersample, 1, 3) - 1;
		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::Combo("Orbital mesh detail##export", &supersampleIndex, supersampleLabels, IM_ARRAYSIZE(supersampleLabels)))
			dialog.orbitalSupersample = supersampleIndex + 1;
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Smooths visible facets on the isosurface by trilinear-upsampling the WAVECAR grid "
				"before meshing. Export/batch export only - the live preview always uses the native "
				"grid so it stays responsive. 3x can take a few seconds per band.");

		constexpr ImGuiColorEditFlags kSwatchFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
		ImGui::TextUnformatted("Spin up colors");
		ImGui::SameLine();
		ImGui::ColorEdit3("Positive##exportup", &dialog.previewState.orbitalChannelUp.positiveLobeColor.x, kSwatchFlags);
		ImGui::SameLine();
		ImGui::TextUnformatted("/");
		ImGui::SameLine();
		ImGui::ColorEdit3("Negative##exportup", &dialog.previewState.orbitalChannelUp.negativeLobeColor.x, kSwatchFlags);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::SliderFloat("Opacity##exportup", &dialog.previewState.orbitalChannelUp.lobeAlpha, 0.05f, 1.0f);

		ImGui::TextUnformatted("Spin down colors");
		ImGui::SameLine();
		ImGui::ColorEdit3("Positive##exportdown", &dialog.previewState.orbitalChannelDown.positiveLobeColor.x, kSwatchFlags);
		ImGui::SameLine();
		ImGui::TextUnformatted("/");
		ImGui::SameLine();
		ImGui::ColorEdit3("Negative##exportdown", &dialog.previewState.orbitalChannelDown.negativeLobeColor.x, kSwatchFlags);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::SliderFloat("Opacity##exportdown", &dialog.previewState.orbitalChannelDown.lobeAlpha, 0.05f, 1.0f);

		// Live preview of whichever band is currently selected in the Electronic Structure panel -
		// "__export_preview__" is already rendered every frame further down in Render(), so this
		// just needs to keep its mesh fresh; no viewport resize / double-render dance needed here
		// (unlike the batch/full-resolution capture path), since that per-frame render is itself
		// both the "establish viewport" and "draw the regenerated mesh" pass, one frame apart.
		if (state->selectedBand >= 0)
		{
			if (upEnabled)
			{
				if (const OrbitalGridData *grid = m_Session->TryGetOrDispatchGrid(*state, 0, state->selectedBand))
				{
					const std::uint64_t key = ElectronicStructureSession::PackGridKey(0, state->selectedBand);
					m_Layer.RegenerateOrbitalIsosurfaceForChannel(
						"__export_preview__", *grid, ElectronicStructureSession::ResolveIsoValue(*state, key), 0,
						dialog.previewState.orbitalChannelUp);
				}
			}
			if (downEnabled)
			{
				if (const OrbitalGridData *grid = m_Session->TryGetOrDispatchGrid(*state, 1, state->selectedBand))
				{
					const std::uint64_t key = ElectronicStructureSession::PackGridKey(1, state->selectedBand);
					m_Layer.RegenerateOrbitalIsosurfaceForChannel(
						"__export_preview__", *grid, ElectronicStructureSession::ResolveIsoValue(*state, key), 1,
						dialog.previewState.orbitalChannelDown);
				}
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Batch export: pick which orbitals to render (one PNG per band)");
		const std::vector<OrbitalRecord> filtered = FilterByLocalizationThreshold(
			*state->data->orbitals, LocalizationThresholdSettings{state->localizationThreshold});
		if (filtered.empty())
		{
			ImGui::TextDisabled("No bands in the current window/threshold.");
			return;
		}

		if (ImGui::SmallButton("Select all"))
		{
			dialog.selectedOrbitalBands.clear();
			for (const OrbitalRecord &record : filtered)
				dialog.selectedOrbitalBands.push_back(record.band);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Select none"))
			dialog.selectedOrbitalBands.clear();

		constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
		if (ImGui::BeginTable("export_orbitals", 2, kTableFlags, ImVec2(0.0f, 140.0f)))
		{
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.0f);
			ImGui::TableSetupColumn("Band");
			ImGui::TableHeadersRow();
			for (const OrbitalRecord &record : filtered)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				bool checked = std::find(
					dialog.selectedOrbitalBands.begin(), dialog.selectedOrbitalBands.end(), record.band) !=
					dialog.selectedOrbitalBands.end();
				char checkboxId[32];
				std::snprintf(checkboxId, sizeof(checkboxId), "##band%d", record.band);
				if (ImGui::Checkbox(checkboxId, &checked))
				{
					if (checked)
						dialog.selectedOrbitalBands.push_back(record.band);
					else
						std::erase(dialog.selectedOrbitalBands, record.band);
				}
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%d", record.band);
			}
			ImGui::EndTable();
		}

		const bool batchRunning = dialog.orbitalBatchExport.running;
		ImGui::BeginDisabled(dialog.selectedOrbitalBands.empty() || batchRunning);
		char exportLabel[64];
		std::snprintf(
			exportLabel, sizeof(exportLabel), "Export %d selected orbitals", static_cast<int>(dialog.selectedOrbitalBands.size()));
		if (ImGui::Button(exportLabel))
		{
			dialog.orbitalBatchExport.pendingBands = dialog.selectedOrbitalBands;
			dialog.orbitalBatchExport.totalCount = static_cast<int>(dialog.orbitalBatchExport.pendingBands.size());
			dialog.orbitalBatchExport.completedCount = 0;
			dialog.orbitalBatchExport.skippedCount = 0;
			dialog.orbitalBatchExport.running = true;
		}
		ImGui::EndDisabled();
		if (batchRunning)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Exporting %d/%d (skipped %d)...",
				dialog.orbitalBatchExport.completedCount, dialog.orbitalBatchExport.totalCount, dialog.orbitalBatchExport.skippedCount);
		}
		else if (dialog.orbitalBatchExport.totalCount > 0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled(
				"Done: exported %d, skipped %d.", dialog.orbitalBatchExport.completedCount, dialog.orbitalBatchExport.skippedCount);
		}
	}

	void ExportImagePanel::Render()
	{
		RenderExportDialogState &dialog = m_Layer.GetExportDialogState();
		SetVisible(dialog.open);
		if (!IsVisible())
			return;

		RendererWindowState *targetWindow = nullptr;
		for (RendererWindowState &windowState : m_Layer.GetWindows())
		{
			if (windowState.windowId == dialog.targetWindowId)
			{
				targetWindow = &windowState;
				break;
			}
		}
		if (targetWindow == nullptr || dialog.previewState.camera == nullptr)
		{
			dialog.open = false;
			SetVisible(false);
			return;
		}

		// Polled unconditionally (not gated on the orbital section being visible/expanded) so a
		// running batch export keeps making progress regardless of which part of the dialog the
		// user is looking at, and so the band table/live preview populate even if
		// ElectronicStructurePanel was never opened for this window.
		ElectronicStructureSession::WindowState *esState =
			m_Session != nullptr ? &m_Session->Update(*targetWindow) : nullptr;

		ImGui::SetNextWindowSize(ImVec2(760.0f, 640.0f), ImGuiCond_FirstUseEver);
		bool stillOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &stillOpen))
		{
			ImGui::End();
			return;
		}
		if (!stillOpen)
		{
			dialog.open = false;
			SetVisible(false);
			ImGui::End();
			return;
		}

		char filenameBuffer[256];
		std::snprintf(filenameBuffer, sizeof(filenameBuffer), "%s", dialog.filename.c_str());
		ImGui::SetNextItemWidth(280.0f);
		if (ImGui::InputText("##filename", filenameBuffer, sizeof(filenameBuffer)))
			dialog.filename = filenameBuffer;
		ImGui::SameLine();
		ImGui::TextDisabled(".png");

		ImGui::TextDisabled("%s", dialog.saveDirectory.String().c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Browse..."))
		{
			try
			{
				const std::string defaultName = (dialog.filename.empty() ? "export" : dialog.filename) + ".png";
				Result<std::optional<Path>> picked = Platform::PickSaveFile(
					dialog.saveDirectory, defaultName, "PNG Image", "png");
				if (picked && picked->has_value())
				{
					const std::filesystem::path &pickedPath = picked->value().Native();
					dialog.saveDirectory = Path(pickedPath.parent_path());
					dialog.filename = pickedPath.stem().string();
				}
			}
			catch (const std::exception &exception)
			{
				DS_LOG_ERROR("Export path picker failed: {}", exception.what());
			}
		}

		static const char *presetLabels[] = {"1920x1080 (Full HD)", "2560x1440 (2K)", "3840x2160 (4K)", "Custom"};
		int presetIndex = static_cast<int>(dialog.preset);
		ImGui::SetNextItemWidth(280.0f);
		if (ImGui::Combo("Resolution", &presetIndex, presetLabels, IM_ARRAYSIZE(presetLabels)))
			dialog.preset = static_cast<RenderExportDialogState::ResolutionPreset>(presetIndex);

		int exportWidth = 1920;
		int exportHeight = 1080;
		switch (dialog.preset)
		{
			case RenderExportDialogState::ResolutionPreset::FullHd1080p:
				exportWidth = 1920;
				exportHeight = 1080;
				break;
			case RenderExportDialogState::ResolutionPreset::QuadHd2K:
				exportWidth = 2560;
				exportHeight = 1440;
				break;
			case RenderExportDialogState::ResolutionPreset::UltraHd4K:
				exportWidth = 3840;
				exportHeight = 2160;
				break;
			case RenderExportDialogState::ResolutionPreset::Custom:
				dialog.customWidth = std::clamp(dialog.customWidth, 64, 8192);
				dialog.customHeight = std::clamp(dialog.customHeight, 64, 8192);
				ImGui::SetNextItemWidth(135.0f);
				ImGui::InputInt("Width", &dialog.customWidth);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(135.0f);
				ImGui::InputInt("Height", &dialog.customHeight);
				exportWidth = dialog.customWidth;
				exportHeight = dialog.customHeight;
				break;
		}

		if (esState != nullptr)
			stepOrbitalBatchExport(*esState, *targetWindow, exportWidth, exportHeight);

		ImGui::Separator();
		ImGui::Checkbox("Atoms##export", &dialog.previewState.showAtoms);
		ImGui::SameLine();
		ImGui::Checkbox("Bonds##export", &dialog.previewState.showBonds);
		ImGui::SameLine();
		ImGui::Checkbox("Cell##export", &dialog.previewState.showCellBox);
		ImGui::SameLine();
		ImGui::Checkbox("Grid##export", &dialog.previewState.showGrid);
		ImGui::SameLine();
		ImGui::Checkbox("Labels##export", &dialog.previewState.showLabels);

		ImGui::Checkbox("Custom background##export", &dialog.useCustomBackground);
		if (dialog.useCustomBackground)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			ImGui::ColorEdit3("##exportBackground", &dialog.backgroundColor.x);
		}

		ImGui::Separator();
		float zoomDistance = dialog.previewState.camera->Distance();
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::SliderFloat("Zoom", &zoomDistance, 0.5f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
			dialog.previewState.camera->SetDistance(zoomDistance);
		ImGui::SameLine();
		if (ImGui::SmallButton("Reset View") && targetWindow->camera != nullptr)
			*dialog.previewState.camera = *targetWindow->camera;

		ImGui::TextDisabled("Crop (trims pixels from each edge - changes output aspect ratio):");
		auto cropSlider = [](const char *label, float &fraction)
		{
			float percent = fraction * 100.0f;
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::SliderFloat(label, &percent, 0.0f, 45.0f, "%.0f%%"))
				fraction = percent * 0.01f;
		};
		cropSlider("Left##crop", dialog.cropLeft);
		ImGui::SameLine();
		cropSlider("Right##crop", dialog.cropRight);
		cropSlider("Top##crop", dialog.cropTop);
		ImGui::SameLine();
		cropSlider("Bottom##crop", dialog.cropBottom);

		renderOrbitalExportControls(esState);

		// The single Export button also includes whichever orbital(s) are currently toggled on
		// (same as the live preview), using the Electronic Structure panel's currently-selected
		// band - if that band's grid isn't cached yet, disable Export rather than capture a frame
		// with a stale/empty mesh.
		const bool wantsOrbitalOverlay =
			dialog.previewState.orbitalChannelUp.enabled || dialog.previewState.orbitalChannelDown.enabled;
		const bool orbitalNotReady = wantsOrbitalOverlay && (esState == nullptr || esState->selectedBand < 0);
		const bool batchRunning = dialog.orbitalBatchExport.running;

		ImGui::Separator();
		ImGui::BeginDisabled(orbitalNotReady || batchRunning);
		if (ImGui::Button("Export", ImVec2(120.0f, 0.0f)))
		{
			try
			{
				bool ready = true;
				if (wantsOrbitalOverlay && esState != nullptr)
					ready = renderOrbitalBandForCapture(
						"__export_full__", esState->selectedBand, *esState, *targetWindow, exportWidth, exportHeight);
				else
				{
					dialog.previewState.viewportSize = glm::vec2(static_cast<float>(exportWidth), static_cast<float>(exportHeight));
					dialog.previewState.camera->SetViewport(static_cast<float>(exportWidth), static_cast<float>(exportHeight));
					(void)m_Layer.RenderToFbo(
						"__export_full__", targetWindow->structure, dialog.previewState, resolveExportSettings());
				}

				if (!ready)
					DS_LOG_WARN("PNG export: orbital data still loading, try again shortly");
				else
				{
					std::string error;
					const std::string safeName = dialog.filename.empty() ? "export" : dialog.filename;
					const Path outputPath = dialog.saveDirectory / (safeName + ".png");
					if (!m_Layer.CaptureWindowToPng(
							"__export_full__", outputPath, error,
							dialog.cropLeft, dialog.cropRight, dialog.cropTop, dialog.cropBottom))
						DS_LOG_ERROR("PNG export failed: {}", error);
					else
						DS_LOG_INFO("PNG export: {}", outputPath.String());
				}
			}
			catch (const std::exception &exception)
			{
				DS_LOG_ERROR("PNG export threw: {}", exception.what());
			}

			ImGui::EndDisabled();
			dialog.open = false;
			SetVisible(false);
			ImGui::End();
			return;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(batchRunning ? "Cancel batch" : "Cancel", ImVec2(120.0f, 0.0f)))
		{
			if (batchRunning)
			{
				dialog.orbitalBatchExport.running = false;
				dialog.orbitalBatchExport.pendingBands.clear();
			}
			else
			{
				dialog.open = false;
				SetVisible(false);
				ImGui::End();
				return;
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("Drag preview to reframe. Aspect matches the chosen resolution - never stretched.");

		// Fill whatever space is left in the (resizable, dockable) panel, letterboxed to the
		// export resolution's aspect ratio - never stretched, and scales as the panel is resized.
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float targetAspect = static_cast<float>(exportWidth) / static_cast<float>(exportHeight);
		float previewWidth = std::max(available.x, 64.0f);
		float previewHeight = previewWidth / targetAspect;
		if (previewHeight > available.y && available.y > 0.0f)
		{
			previewHeight = std::max(available.y, 64.0f);
			previewWidth = previewHeight * targetAspect;
		}

		dialog.previewState.viewportSize = glm::vec2(previewWidth, previewHeight);
		dialog.previewState.camera->SetViewport(previewWidth, previewHeight);

		unsigned int previewTexture = 0;
		try
		{
			previewTexture = m_Layer.RenderToFbo(
				"__export_preview__", targetWindow->structure, dialog.previewState, resolveExportSettings());
		}
		catch (const std::exception &exception)
		{
			DS_LOG_ERROR("Export dialog preview render failed: {}", exception.what());
			ImGui::End();
			return;
		}

		const ImVec2 previewMin = ImGui::GetCursorScreenPos();
		ImGui::Image(
			static_cast<ImTextureID>(static_cast<uintptr_t>(previewTexture)),
			ImVec2(previewWidth, previewHeight),
			ImVec2(0.0f, 1.0f),
			ImVec2(1.0f, 0.0f));
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			dialog.previewState.camera->Pan(
				delta.x * m_Layer.GetGlobalSettings().panSensitivity,
				delta.y * m_Layer.GetGlobalSettings().panSensitivity);
		}

		// Dim the margins that Export will crop away - shows the kept region without re-rendering
		// or resizing the preview (which would misrepresent the actual output aspect ratio).
		if (dialog.cropLeft > 0.0f || dialog.cropRight > 0.0f || dialog.cropTop > 0.0f || dialog.cropBottom > 0.0f)
		{
			ImDrawList *drawList = ImGui::GetWindowDrawList();
			const ImU32 dimColor = IM_COL32(0, 0, 0, 160);
			const float leftPx = previewWidth * dialog.cropLeft;
			const float rightPx = previewWidth * dialog.cropRight;
			const float topPx = previewHeight * dialog.cropTop;
			const float bottomPx = previewHeight * dialog.cropBottom;
			const ImVec2 previewMax(previewMin.x + previewWidth, previewMin.y + previewHeight);
			if (leftPx > 0.0f)
				drawList->AddRectFilled(previewMin, ImVec2(previewMin.x + leftPx, previewMax.y), dimColor);
			if (rightPx > 0.0f)
				drawList->AddRectFilled(ImVec2(previewMax.x - rightPx, previewMin.y), previewMax, dimColor);
			if (topPx > 0.0f)
				drawList->AddRectFilled(previewMin, ImVec2(previewMax.x, previewMin.y + topPx), dimColor);
			if (bottomPx > 0.0f)
				drawList->AddRectFilled(ImVec2(previewMin.x, previewMax.y - bottomPx), previewMax, dimColor);
		}

		ImGui::End();
	}
} // namespace DefectStudio
