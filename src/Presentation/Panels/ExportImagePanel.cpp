#include "Core/dspch.hpp"

#include "Presentation/Panels/ExportImagePanel.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include <imgui.h>

#include "Core/Logging/Logger.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Core/Utils/Path.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	ExportImagePanel::ExportImagePanel(
		RendererLayer &layer,
		Ref<EventBus> eventBus,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_EventBus(std::move(eventBus))
	{
	}

	Ref<IPanel> ExportImagePanel::Clone() const
	{
		return CreateRef<ExportImagePanel>(*this);
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

		ImGui::Separator();
		ImGui::Checkbox("Atoms##export", &dialog.previewState.showAtoms);
		ImGui::SameLine();
		ImGui::Checkbox("Bonds##export", &dialog.previewState.showBonds);
		ImGui::SameLine();
		ImGui::Checkbox("Cell##export", &dialog.previewState.showCellBox);
		ImGui::SameLine();
		ImGui::Checkbox("Grid##export", &dialog.previewState.showGrid);

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

		ImGui::Separator();
		if (ImGui::Button("Export", ImVec2(120.0f, 0.0f)))
		{
			try
			{
				dialog.previewState.viewportSize = glm::vec2(static_cast<float>(exportWidth), static_cast<float>(exportHeight));
				dialog.previewState.camera->SetViewport(static_cast<float>(exportWidth), static_cast<float>(exportHeight));
				m_Layer.RenderToFbo(
					"__export_full__", targetWindow->structure, dialog.previewState, m_Layer.GetGlobalSettings());

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
			catch (const std::exception &exception)
			{
				DS_LOG_ERROR("PNG export threw: {}", exception.what());
			}

			dialog.open = false;
			SetVisible(false);
			ImGui::End();
			return;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		{
			dialog.open = false;
			SetVisible(false);
			ImGui::End();
			return;
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
				"__export_preview__", targetWindow->structure, dialog.previewState, m_Layer.GetGlobalSettings());
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
