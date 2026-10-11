#include "Core/dspch.hpp"

#include "Presentation/Panels/PropertyGrid.hpp"

#include <algorithm>

#include <imgui.h>

namespace DefectStudio
{
	float PropertyGridLabelWidth(const std::initializer_list<const char *> labels)
	{
		const ImGuiStyle &style = ImGui::GetStyle();
		float widest = 0.0f;
		for (const char *label : labels)
			widest = std::max(widest, ImGui::CalcTextSize(label).x);
		// Never more than ~40 % of the panel: a long label wraps the field column, not the other way.
		const float cap = ImGui::GetContentRegionAvail().x * 0.4f;
		return std::min(widest + style.CellPadding.x * 2.0f + style.ItemSpacing.x, std::max(cap, 60.0f));
	}

	PropertyGrid::PropertyGrid(const char *id, const float labelWidth)
	{
		m_Open = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings);
		if (!m_Open)
			return;
		ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
		ImGui::TableSetupColumn("##field", ImGuiTableColumnFlags_WidthStretch);
	}

	PropertyGrid::~PropertyGrid()
	{
		if (m_Open)
			ImGui::EndTable();
	}

	void PropertyGrid::Row(const char *label, const char *tooltip)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		if (tooltip != nullptr)
			ImGui::SetItemTooltip("%s", tooltip);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	void PropertyGrid::Value(const char *label, const char *value, const char *tooltip)
	{
		Row(label, tooltip);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(value);
	}
} // namespace DefectStudio
