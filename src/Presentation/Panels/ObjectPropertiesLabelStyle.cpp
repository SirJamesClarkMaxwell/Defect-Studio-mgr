#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesLabelStyle.hpp"

#include <imgui.h>

namespace DefectStudio
{
	// Shared by every label kind (free labels, pinned bond/angle labels) - one editor for
	// RendererWindowState::LabelStyle instead of a separate control block per label kind. Outline/
	// background rows read "0 = off" like the shader they feed (labels.frag/label_background.frag).
	void DrawLabelStyleEditor(RendererWindowState::LabelStyle &style)
	{
		ImGui::TextUnformatted("Text");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleTextColor", &style.textColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::SliderFloat("Alpha##StyleTextAlpha", &style.textAlpha, 0.0f, 1.0f, "%.2f");

		// Checkbox is a thin view over backgroundAlpha/outlineWidth themselves (0 = off, same meaning
		// the shader already gives that value) rather than a separate enabled flag - one source of
		// truth. Toggling on restores a sensible default rather than 0, since the slider/drag below
		// would otherwise show "on" at a still-invisible value.
		bool backgroundEnabled = style.backgroundAlpha > 0.0f;
		if (ImGui::Checkbox("##StyleBackgroundEnabled", &backgroundEnabled))
			style.backgroundAlpha = backgroundEnabled ? 0.85f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Background");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::BeginDisabled(!backgroundEnabled);
		ImGui::ColorEdit3("##StyleBackgroundColor", &style.backgroundColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::SliderFloat("Alpha##StyleBackgroundAlpha", &style.backgroundAlpha, 0.01f, 1.0f, "%.2f");
		ImGui::EndDisabled();

		bool borderEnabled = style.outlineWidth > 0.0f;
		if (ImGui::Checkbox("##StyleBorderEnabled", &borderEnabled))
			style.outlineWidth = borderEnabled ? 0.02f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Border");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleOutlineColor", &style.outlineColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!borderEnabled);
		ImGui::DragFloat("Width##StyleOutlineWidth", &style.outlineWidth, 0.002f, 0.001f, 0.2f, "%.3f");
		ImGui::EndDisabled();

		// Glyph stroke (labels.frag), independent of the Border row above which only frames the
		// background quad. Screen pixels, not world units and not normalized SDF units - stays a
		// constant on-screen thickness regardless of zoom, unlike Border's 0.001-0.2 world-unit range.
		bool strokeEnabled = style.strokeWidth > 0.0f;
		if (ImGui::Checkbox("##StyleStrokeEnabled", &strokeEnabled))
			style.strokeWidth = strokeEnabled ? 2.0f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Stroke");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleStrokeColor", &style.strokeColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!strokeEnabled);
		ImGui::DragFloat("Width (px)##StyleStrokeWidth", &style.strokeWidth, 0.05f, 0.1f, 8.0f, "%.2f");
		ImGui::EndDisabled();

		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("Corner radius##StyleCornerRadius", &style.cornerRadius, 0.005f, 0.0f, 0.3f, "%.3f");

		ImGui::SetNextItemWidth(140.0f);
		ImGui::DragFloat2("Padding X/Y##StylePadding", &style.padding.x, 0.005f, 0.0f, 1.0f, "%.3f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("Scale##StyleScale", &style.scale, 0.02f, 0.1f, 8.0f, "%.2f");
	}

} // namespace DefectStudio
