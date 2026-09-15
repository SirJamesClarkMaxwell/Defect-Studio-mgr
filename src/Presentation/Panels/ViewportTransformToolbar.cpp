#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportToolbars.hpp"

#include <array>

#include <imgui.h>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	void DrawViewportTransformControls(RendererWindowState &windowState, float uiScale)
	{
		constexpr std::array<const char *, 3> orientationNames = {"Global", "Local", "Lattice"};
		constexpr std::array<const char *, 3> pivotNames = {"Median", "3D Cursor", "Individual"};
		int orientation = static_cast<int>(windowState.transformOrientation);
		int pivot = static_cast<int>(windowState.transformPivotMode);

		ImGui::BeginDisabled(windowState.modalTransform.has_value());
		ImGui::SetNextItemWidth(88.0f * uiScale);
		if (ImGui::Combo(
				"##TransformOrientation", &orientation, orientationNames.data(),
				static_cast<int>(orientationNames.size())))
			windowState.transformOrientation = static_cast<TransformOrientation>(orientation);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Transform Orientation");

		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 0.70f);
		ImGui::SetNextItemWidth(100.0f * uiScale);
		if (ImGui::Combo("##TransformPivot", &pivot, pivotNames.data(), static_cast<int>(pivotNames.size())))
			windowState.transformPivotMode = static_cast<TransformPivotMode>(pivot);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Transform Pivot");
		ImGui::EndDisabled();
	}
} // namespace DefectStudio
