#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"

#include <algorithm>

#include <imgui.h>

namespace DefectStudio
{
	namespace
	{
		enum class VisibilityIcon
		{
			Eye,
			Camera,
		};

		void DrawEyeIcon(ImDrawList &drawList, const ImVec2 &minimum, const ImVec2 &maximum, ImU32 color)
		{
			const ImVec2 center((minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f);
			const float halfWidth = (maximum.x - minimum.x) * 0.31f;
			const float halfHeight = (maximum.y - minimum.y) * 0.20f;
			const ImVec2 points[4] = {
				ImVec2(center.x - halfWidth, center.y), ImVec2(center.x, center.y - halfHeight),
				ImVec2(center.x + halfWidth, center.y), ImVec2(center.x, center.y + halfHeight)};
			drawList.AddPolyline(
				points, 4, color, ImDrawFlags_Closed, 1.5f);
			drawList.AddCircleFilled(center, std::max(1.5f, halfHeight * 0.45f), color);
		}

		void DrawCameraIcon(ImDrawList &drawList, const ImVec2 &minimum, const ImVec2 &maximum, ImU32 color)
		{
			const float width = maximum.x - minimum.x;
			const float height = maximum.y - minimum.y;
			const ImVec2 bodyMin(minimum.x + width * 0.20f, minimum.y + height * 0.31f);
			const ImVec2 bodyMax(maximum.x - width * 0.16f, maximum.y - height * 0.22f);
			drawList.AddRect(bodyMin, bodyMax, color, 1.5f, ImDrawFlags_None, 1.5f);
			drawList.AddRectFilled(
				ImVec2(bodyMin.x + width * 0.08f, bodyMin.y - height * 0.12f),
				ImVec2(bodyMin.x + width * 0.27f, bodyMin.y), color, 1.0f);
			drawList.AddCircle(
				ImVec2((bodyMin.x + bodyMax.x) * 0.5f, (bodyMin.y + bodyMax.y) * 0.5f),
				height * 0.14f, color, 0, 1.5f);
		}

		bool DrawColumn(
			const char *id, const char *tooltip, const ImVec2 &position, const ImVec2 &size,
			const VisibilityIcon icon, const bool value, const bool mixed)
		{
			ImGui::SetCursorScreenPos(position);
			const bool changed = ImGui::InvisibleButton(id, size);
			const ImVec2 minimum = ImGui::GetItemRectMin();
			const ImVec2 maximum = ImGui::GetItemRectMax();
			ImDrawList &drawList = *ImGui::GetWindowDrawList();
			if (ImGui::IsItemHovered())
				drawList.AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 2.0f);

			const ImU32 color = ImGui::GetColorU32(value || mixed ? ImGuiCol_Text : ImGuiCol_TextDisabled);
			if (icon == VisibilityIcon::Eye)
				DrawEyeIcon(drawList, minimum, maximum, color);
			else
				DrawCameraIcon(drawList, minimum, maximum, color);
			if (!value && !mixed)
			{
				drawList.AddLine(
					ImVec2(minimum.x + size.x * 0.20f, maximum.y - size.y * 0.20f),
					ImVec2(maximum.x - size.x * 0.20f, minimum.y + size.y * 0.20f), color, 1.5f);
			}
			if (mixed)
			{
				const float y = maximum.y - size.y * 0.20f;
				drawList.AddLine(
					ImVec2(minimum.x + size.x * 0.28f, y), ImVec2(maximum.x - size.x * 0.28f, y),
					ImGui::GetColorU32(ImGuiCol_CheckMark), 2.0f);
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("%s", tooltip);
			return changed;
		}
	} // namespace

	SceneVisibilityColumnEdit DrawSceneVisibilityColumns(const SceneVisibilityColumnState &state)
	{
		SceneVisibilityColumnEdit edit;
		const ImVec2 nextRow = ImGui::GetCursorScreenPos();
		const ImVec2 rowMinimum = ImGui::GetItemRectMin();
		const ImVec2 rowMaximum = ImGui::GetItemRectMax();
		const float rowHeight = std::max(rowMaximum.y - rowMinimum.y, ImGui::GetFrameHeight());
		const ImVec2 buttonSize(rowHeight, rowHeight);
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const float firstX = rowMaximum.x - buttonSize.x * 2.0f - spacing;
		// A mixed column reads as unchecked, so clicking it turns everything under it on - the same
		// direction Blender's outliner takes from a mixed parent.
		edit.visible = state.visibleMixed ? true : !state.visible;
		edit.renderable = state.renderableMixed ? true : !state.renderable;
		edit.visibleChanged = DrawColumn(
			"##outlinerVisible", "Visible in the viewport (H)", ImVec2(firstX, rowMinimum.y), buttonSize,
			VisibilityIcon::Eye, state.visible, state.visibleMixed);
		edit.renderableChanged = DrawColumn(
			"##outlinerRenderable", "Included in an exported render",
			ImVec2(firstX + buttonSize.x + spacing, rowMinimum.y), buttonSize,
			VisibilityIcon::Camera, state.renderable, state.renderableMixed);
		ImGui::SetCursorScreenPos(nextRow);
		return edit;
	}

	bool DrawSceneVisibilityColumns(bool &visible, bool &renderable)
	{
		const SceneVisibilityColumnEdit edit = DrawSceneVisibilityColumns(
			SceneVisibilityColumnState{visible, renderable, false, false});
		if (edit.visibleChanged)
			visible = edit.visible;
		if (edit.renderableChanged)
			renderable = edit.renderable;
		return edit.visibleChanged || edit.renderableChanged;
	}
} // namespace DefectStudio
