#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportToolbars.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include <imgui.h>

#include "IconsFontAwesome6.h"

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Events/RendererEvents.hpp"
#include "Presentation/Panels/ViewportToolbarPopover.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	namespace
	{
		void SelectOrientationRow(
			RendererWindowState &windowState, TransformOrientation value, const char *icon, const char *name)
		{
			const std::string label = std::string(icon) + "  " + name;
			if (ImGui::Selectable(label.c_str(), windowState.transformOrientation == value))
				windowState.transformOrientation = value;
		}

		void SelectPivotRow(RendererWindowState &windowState, TransformPivotMode value, const char *icon, const char *name)
		{
			const std::string label = std::string(icon) + "  " + name;
			if (ImGui::Selectable(label.c_str(), windowState.transformPivotMode == value))
				windowState.transformPivotMode = value;
		}
	}

	void DrawViewportTransformControls(RendererWindowState &windowState, RendererLayer &layer, float uiScale)
	{
		constexpr std::array<const char *, 4> orientationNames = {"Global", "Local", "Lattice", "Defect"};
		constexpr std::array<const char *, 4> orientationGlyphs = {
			ICON_FA_GLOBE, ICON_FA_ARROWS_ROTATE, ICON_FA_BORDER_ALL, ICON_FA_LOCATION_CROSSHAIRS};
		const int orientationIndex = static_cast<int>(windowState.transformOrientation);
		const char *orientationGlyph = orientationGlyphs[static_cast<std::size_t>(orientationIndex)];
		const float iconExtent = std::clamp(layer.GetGlobalSettings().viewport.iconButtonSize, 12.0f, 40.0f) * uiScale;

		ImGui::BeginDisabled(windowState.modalTransform.has_value());
		if (BeginViewportToolbarPopover({
				"##TransformOrientationButton", "##TransformOrientationPopup", 0u,
				orientationGlyph, orientationNames[static_cast<std::size_t>(orientationIndex)], "Transform Orientation",
				uiScale, iconExtent, 180.0f}))
		{
			ImGui::SeparatorText("Transform Orientation");
			SelectOrientationRow(windowState, TransformOrientation::Global, ICON_FA_GLOBE, "Global");
			SelectOrientationRow(windowState, TransformOrientation::Local, ICON_FA_ARROWS_ROTATE, "Local");
			SelectOrientationRow(windowState, TransformOrientation::Lattice, ICON_FA_BORDER_ALL, "Lattice");
			ImGui::BeginDisabled(!windowState.structure.defectFrame.has_value());
			SelectOrientationRow(windowState, TransformOrientation::Defect, ICON_FA_LOCATION_CROSSHAIRS, "Defect axes");
			ImGui::EndDisabled();
			if (!windowState.structure.defectFrame.has_value() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Najpierw dodaj osie defektu: Shift+A albo PPM > Add > Defect axes (empty).");
			ImGui::EndPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 0.70f);
		const char *pivotGlyph = windowState.transformPivotMode == TransformPivotMode::Cursor3D
			? ICON_FA_CROSSHAIRS
			: windowState.transformPivotMode == TransformPivotMode::IndividualOrigins ? ICON_FA_BRAILLE : ICON_FA_CIRCLE_DOT;
		ImGui::BeginDisabled(windowState.modalTransform.has_value());
		if (BeginViewportToolbarPopover({
				"##TransformPivotButton", "##TransformPivotPopup", 0u, pivotGlyph, nullptr,
				"Transform Pivot", uiScale, iconExtent, 190.0f}))
		{
			ImGui::SeparatorText("Pivot Point");
			SelectPivotRow(windowState, TransformPivotMode::Median, ICON_FA_CIRCLE_DOT, "Median");
			SelectPivotRow(windowState, TransformPivotMode::Cursor3D, ICON_FA_CROSSHAIRS, "3D Cursor");
			SelectPivotRow(windowState, TransformPivotMode::IndividualOrigins, ICON_FA_BRAILLE, "Individual Origins");
			ImGui::EndPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 0.70f);
		if (BeginViewportToolbarPopover({
				"##TransformSnapButton", "##TransformSnapPopup", 0u, ICON_FA_MAGNET, nullptr,
				"Transform Snap Steps (hold Ctrl during G/R/S)", uiScale, iconExtent, 220.0f}))
		{
			struct SnapDraft
			{
				float translate = 0.1f;
				float rotate = 5.0f;
				float scale = 0.1f;
			};
			static SnapDraft draft;
			if (ImGui::IsWindowAppearing())
			{
				const RendererViewportSettings &settings = layer.GetGlobalSettings().viewport;
				draft = {settings.transformTranslateSnap, settings.transformRotateSnapDegrees, settings.transformScaleSnap};
			}

			ImGui::SeparatorText("Transform Snapping");
			ImGui::TextDisabled("Hold Ctrl during G / R / S");
			bool finishedEdit = false;
			if (ImGui::BeginTable("##TransformSnapSteps", 2, ImGuiTableFlags_SizingStretchProp))
			{
				auto row = [&finishedEdit](const char *label, const char *id, float &value, float maximum, const char *format)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::DragFloat(id, &value, value < 1.0f ? 0.01f : 0.1f, 0.0001f, maximum, format);
					finishedEdit = finishedEdit || ImGui::IsItemDeactivatedAfterEdit();
				};
				row("G [A]", "##TranslateSnap", draft.translate, 1000.0f, "%.4g");
				row("R [deg]", "##RotateSnap", draft.rotate, 180.0f, "%.4g");
				row("S", "##ScaleSnap", draft.scale, 10.0f, "%.4g");
				ImGui::EndTable();
			}
			if (finishedEdit)
			{
				draft.translate = std::clamp(draft.translate, 0.0001f, 1000.0f);
				draft.rotate = std::clamp(draft.rotate, 0.0001f, 180.0f);
				draft.scale = std::clamp(draft.scale, 0.0001f, 10.0f);
				if (Ref<EventBus> eventBus = layer.GetEventBus())
				{
					RendererEvents::Config::TransformSnapStepsChanged event;
					event.translate = draft.translate;
					event.rotateDegrees = draft.rotate;
					event.scale = draft.scale;
					eventBus->Publish(event);
				}
			}
			ImGui::EndPopup();
		}
	}
} // namespace DefectStudio
