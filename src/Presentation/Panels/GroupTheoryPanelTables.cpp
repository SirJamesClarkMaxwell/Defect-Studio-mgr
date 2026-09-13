#include "Core/dspch.hpp"

#include "Presentation/Panels/GroupTheoryPanel.hpp"

#include <cstdio>
#include <string>

#include <imgui.h>

#include "Presentation/Panels/GroupTheoryFormatting.hpp"

namespace DefectStudio
{
	namespace
	{
		void Cell(const ExactCoefficient &value)
		{
			ImGui::TextUnformatted(value.exact.c_str());
			if (ImGui::IsItemHovered())
			{
				char numeric[96];
				if (value.numericImaginary != 0.0)
					std::snprintf(numeric, sizeof(numeric), "%.8g %+.8gi", value.numeric, value.numericImaginary);
				else
					std::snprintf(numeric, sizeof(numeric), "%.8g", value.numeric);
				ImGui::SetTooltip("%s", numeric);
			}
		}

		[[nodiscard]] std::string DetectionText(const PointGroupAnalysisResult &result)
		{
			const PointGroupDetection &detection = result.detection;
			if (!detection.determined)
				return "Undetermined: " + detection.reason;
			char tolerance[32];
			std::snprintf(tolerance, sizeof(tolerance), "%.3g", detection.tolerance);
			if (!detection.ran)
				return "Point group: " + FormatIrrepLabel(detection.pointGroupLabel, LabelStyle::Unicode) + " (manual)";
			return "Point group: " + FormatIrrepLabel(detection.pointGroupLabel, LabelStyle::Unicode) +
				" (detected, pymatgen " + detection.detectorSymbol + ", tol " + tolerance + " Å)";
		}
	}

	void GroupTheoryPanel::drawResults()
	{
		const std::string detectionText = DetectionText(*m_Result);
		if (!m_Result->detection.determined)
		{
			ImGui::TextWrapped("%s", detectionText.c_str());
			return;
		}
		ImGui::TextWrapped("%s", detectionText.c_str());
		drawCharacterTable();
		ImGui::TextUnformatted(FormatDecomposition(m_Result->reduction.decomposition, LabelStyle::Unicode).c_str());
		drawProjectedVectors();
		drawMultiplets();
		if (ImGui::Button("Copy as Markdown"))
			copyResults(TableFormat::Markdown);
		ImGui::SameLine();
		if (ImGui::Button("Copy as LaTeX"))
			copyResults(TableFormat::Latex);
	}

	void GroupTheoryPanel::drawCharacterTable()
	{
		const CharacterTable &table = m_Result->characterTable;
		const int columns = static_cast<int>(table.classLabels.size()) + 1;
		if (!ImGui::BeginTable(
				"##group_character_table",
				columns,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			return;
		ImGui::TableSetupColumn("Irrep");
		for (const std::string &label : table.classLabels)
			ImGui::TableSetupColumn(FormatClassLabel(label, LabelStyle::Unicode).c_str());
		ImGui::TableHeadersRow();
		for (std::size_t row = 0; row < table.irrepLabels.size(); ++row)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(FormatIrrepLabel(table.irrepLabels[row], LabelStyle::Unicode).c_str());
			for (std::size_t column = 0; column < table.characters[row].size(); ++column)
			{
				ImGui::TableSetColumnIndex(static_cast<int>(column + 1));
				Cell(table.characters[row][column]);
			}
		}
		if (!m_Result->reducibleCharacters.empty())
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Γ");
			for (std::size_t column = 0; column < m_Result->reducibleCharacters.size(); ++column)
			{
				ImGui::TableSetColumnIndex(static_cast<int>(column + 1));
				Cell(m_Result->reducibleCharacters[column]);
			}
		}
		ImGui::EndTable();
	}

	void GroupTheoryPanel::drawProjectedVectors()
	{
		const auto &vectors = m_Result->reduction.projectedVectors;
		if (vectors.empty())
			return;
		ImGui::TextUnformatted("Projected vectors");
		const int columns = static_cast<int>(m_Result->reduction.siteLabels.size()) + 4;
		if (!ImGui::BeginTable(
				"##group_projected_vectors",
				columns,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			return;
		ImGui::TableSetupColumn("Irrep");
		ImGui::TableSetupColumn("Copy");
		ImGui::TableSetupColumn("Row");
		for (const std::string &label : m_Result->reduction.siteLabels)
			ImGui::TableSetupColumn(label.c_str());
		ImGui::TableSetupColumn("Physical label");
		ImGui::TableHeadersRow();
		for (std::size_t row = 0; row < vectors.size(); ++row)
		{
			const SymmetryAdaptedVector &vector = vectors[row];
			ImGui::PushID(static_cast<int>(row));
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(FormatIrrepLabel(vector.irrepLabel, LabelStyle::Unicode).c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", vector.occurrenceIndex + 1);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%d", vector.irrepRow + 1);
			for (std::size_t column = 0; column < vector.coefficients.size(); ++column)
			{
				ImGui::TableSetColumnIndex(static_cast<int>(column + 3));
				Cell(vector.coefficients[column]);
			}
			ImGui::TableSetColumnIndex(columns - 1);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##physical_label", m_PhysicalBuffers[row].data(), m_PhysicalBuffers[row].size());
			if (m_PhysicalBuffers[row][0] != '\0')
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(assumption)");
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	void GroupTheoryPanel::drawMultiplets()
	{
		if (m_Result->multiplets.empty())
			return;
		ImGui::TextUnformatted("Multiplets");
		if (ImGui::BeginTable("##group_multiplets", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Term");
			ImGui::TableSetupColumn("Count per row");
			ImGui::TableSetupColumn("States");
			ImGui::TableHeadersRow();
			for (const MultipletTerm &term : m_Result->multiplets)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(FormatTermLabel(term.spinMultiplicity, term.irrepLabel, LabelStyle::Unicode).c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%d", term.countPerRow);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%d", term.totalStates);
			}
			ImGui::EndTable();
		}
		ImGui::Text("Total states: %d", m_Result->multipletTotalStates);
	}

	void GroupTheoryPanel::copyResults(TableFormat format)
	{
		const LabelStyle style = format == TableFormat::Markdown ? LabelStyle::Unicode : LabelStyle::Latex;
		const std::string decomposition = FormatDecomposition(m_Result->reduction.decomposition, style);
		const std::string table = FormatCharacterTable(m_Result->characterTable, m_Result->reducibleCharacters, format);
		const std::string multiplets = FormatMultiplets(m_Result->multiplets, format);
		ImGui::SetClipboardText((decomposition + "\n\n" + table + (m_Result->multiplets.empty() ? "" : "\n" + multiplets)).c_str());
	}
} // namespace DefectStudio
