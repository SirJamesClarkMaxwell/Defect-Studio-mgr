#include "Core/dspch.hpp"

#include <algorithm>

#include <imgui.h>

#include "Presentation/Panels/SettingsPanel.hpp"

namespace DefectStudio
{
	bool SettingsPanel::renderRendererViewportSettings()
	{
		bool rendererSettingsChanged = false;
		auto markDirty = [this, &rendererSettingsChanged]() {
			m_DraftDirty = true;
			rendererSettingsChanged = true;
		};
		const auto beginRendererTable = [](const char *id) -> bool
		{
			if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
				return false;
			ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 230.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			return true;
		};
		const auto setValueControlWidth = []()
		{
			const float maxWidth = std::min(420.0f, ImGui::GetContentRegionAvail().x);
			ImGui::SetNextItemWidth(std::max(160.0f, maxWidth));
		};

		ImGui::SeparatorText("Viewport gizmo");
		if (beginRendererTable("RendererViewport"))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Axis button size");
			ImGui::TableSetColumnIndex(1);
			setValueControlWidth();
			if (ImGui::SliderFloat("##AxisButtonSize", &m_DraftConfig.renderer.viewport.axisButtonSize, 10.0f, 48.0f, "%.0f"))
				markDirty();

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Icon button size");
			ImGui::TableSetColumnIndex(1);
			setValueControlWidth();
			if (ImGui::SliderFloat("##IconButtonSize", &m_DraftConfig.renderer.viewport.iconButtonSize, 10.0f, 48.0f, "%.0f"))
				markDirty();

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Transform gizmo size");
			ImGui::TableSetColumnIndex(1);
			setValueControlWidth();
			if (ImGui::SliderFloat(
					"##TransformGizmoSize", &m_DraftConfig.renderer.viewport.transformGizmoSize, 40.0f, 300.0f, "%.0f"))
				markDirty();

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Navigation gizmo size");
			ImGui::TableSetColumnIndex(1);
			setValueControlWidth();
			if (ImGui::SliderFloat(
					"##NavigationGizmoSize", &m_DraftConfig.renderer.viewport.navigationGizmoSize, 30.0f, 150.0f, "%.0f"))
				markDirty();

			ImGui::EndTable();
		}

		return rendererSettingsChanged;
	}
} // namespace DefectStudio
