#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportToolbarPopover.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdint>

#include <imgui.h>

namespace DefectStudio
{
	bool BeginViewportToolbarPopover(const ViewportToolbarPopoverSpec &spec)
	{
		const float scale = std::max(spec.uiScale, 0.01f);
		const ImGuiStyle &style = ImGui::GetStyle();
		const float iconExtent = std::max(spec.iconExtent, 12.0f * scale);
		const bool hasText = spec.text != nullptr && spec.text[0] != '\0';
		const ImVec2 textSize = hasText ? ImGui::CalcTextSize(spec.text) : ImVec2(0.0f, 0.0f);
		const float arrowWidth = 11.0f * scale;
		const float innerGap = 5.0f * scale;
		const float width = style.FramePadding.x * 2.0f + iconExtent + arrowWidth +
			(hasText ? innerGap + textSize.x : 0.0f);
		const float height = std::max(iconExtent, textSize.y) + style.FramePadding.y * 2.0f;

		const ImVec2 minimum = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(spec.buttonId, ImVec2(width, height));
		const ImVec2 maximum = ImGui::GetItemRectMax();
		const bool hovered = ImGui::IsItemHovered();
		const bool held = ImGui::IsItemActive();
		const ImU32 background = ImGui::GetColorU32(
			held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
		ImDrawList *drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(minimum, maximum, background, style.FrameRounding);

		const ImVec2 iconMinimum(
			minimum.x + style.FramePadding.x,
			minimum.y + (height - iconExtent) * 0.5f);
		const ImVec2 iconMaximum(iconMinimum.x + iconExtent, iconMinimum.y + iconExtent);
		if (spec.textureId != 0)
		{
			const ImTextureRef texture(reinterpret_cast<void *>(static_cast<uintptr_t>(spec.textureId)));
			drawList->AddImage(
				texture, iconMinimum, iconMaximum,
				ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
		}
		else if (spec.fallbackIcon != nullptr)
		{
			const ImVec2 fallbackSize = ImGui::CalcTextSize(spec.fallbackIcon);
			drawList->AddText(
				ImVec2(
					iconMinimum.x + (iconExtent - fallbackSize.x) * 0.5f,
					iconMinimum.y + (iconExtent - fallbackSize.y) * 0.5f),
				ImGui::GetColorU32(ImGuiCol_Text), spec.fallbackIcon);
		}
		if (hasText)
		{
			drawList->AddText(
				ImVec2(iconMaximum.x + innerGap, minimum.y + (height - textSize.y) * 0.5f),
				ImGui::GetColorU32(ImGuiCol_Text), spec.text);
		}

		const float arrowX = maximum.x - style.FramePadding.x - arrowWidth * 0.5f;
		const float arrowY = minimum.y + height * 0.5f;
		drawList->AddTriangleFilled(
			ImVec2(arrowX - 4.5f * scale, arrowY - 2.0f * scale),
			ImVec2(arrowX + 4.5f * scale, arrowY - 2.0f * scale),
			ImVec2(arrowX, arrowY + 3.5f * scale), ImGui::GetColorU32(ImGuiCol_TextDisabled));

		if (hovered && spec.tooltip != nullptr && spec.tooltip[0] != '\0')
			ImGui::SetTooltip("%s", spec.tooltip);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			ImGui::OpenPopup(spec.popupId);

		ImGui::SetNextWindowPos(ImVec2(minimum.x, maximum.y), ImGuiCond_Appearing);
		ImGui::SetNextWindowSizeConstraints(
			ImVec2(spec.minimumPopupWidth * scale, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		return ImGui::BeginPopup(spec.popupId, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
	}
} // namespace DefectStudio
