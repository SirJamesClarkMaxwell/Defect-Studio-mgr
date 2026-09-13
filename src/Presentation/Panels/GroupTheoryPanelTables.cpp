#include "Core/dspch.hpp"

#include "Presentation/Panels/GroupTheoryPanel.hpp"

#include <cstdio>
#include <numeric>
#include <string>

#include <imgui.h>

#include "Presentation/Panels/GroupTheoryFormatting.hpp"

namespace DefectStudio
{
	namespace
	{
		void MathLabel(std::string_view latex)
		{
			const float baseY = ImGui::GetCursorPosY();
			const float baseSize = ImGui::GetFontSize();
			ImFont *font = ImGui::GetFont();
			ImGui::BeginGroup();
			bool first = true;
			for (const MathSegment &segment : SplitMathSegments(latex))
			{
				if (!first)
					ImGui::SameLine(0.0f, 0.0f);
				ImGui::SetCursorPosY(baseY + (segment.level > 0 ? -0.25f : segment.level < 0 ? 0.2f : 0.0f) * baseSize);
				if (font != nullptr && segment.level != 0)
					ImGui::PushFont(font, baseSize * 0.7f);
				ImGui::TextUnformatted(segment.text.c_str());
				if (font != nullptr && segment.level != 0)
					ImGui::PopFont();
				first = false;
			}
			ImGui::SetCursorPosY(baseY);
			ImGui::EndGroup();
		}

		void Cell(const ExactCoefficient &value)
		{
			ImGui::TextUnformatted(FormatExactValue(value, LabelStyle::Unicode).c_str());
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
			char tolerance[128];
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
		ImGui::TextUnformatted("Point group: ");
		ImGui::SameLine(0.0f, 0.0f);
		MathLabel(FormatIrrepLabel(m_Result->detection.pointGroupLabel, LabelStyle::Latex));
		ImGui::SameLine(0.0f, 0.0f);
		if (!m_Result->detection.ran)
			ImGui::TextUnformatted(" (manual)");
		else
		{
			char tolerance[128];
			std::snprintf(tolerance, sizeof(tolerance), " (detected, pymatgen %s, tol %.3g Å)",
				m_Result->detection.detectorSymbol.c_str(), m_Result->detection.tolerance);
			ImGui::TextUnformatted(tolerance);
		}
		drawCharacterTable();
		MathLabel(FormatDecomposition(m_Result->reduction.decomposition, LabelStyle::Latex));
		ImGui::SameLine();
		if (ImGui::Button("Use Γ as active space"))
		{
			const std::string active = ActiveIrrepsFromDecomposition(m_Result->reduction.decomposition);
			std::snprintf(m_ActiveIrreps.data(), m_ActiveIrreps.size(), "%s", active.c_str());
		}
		if (m_Result->tensorPower > 0)
		{
			MathLabel(FormatTensorPower(m_Result->tensorPower, m_Result->tensorPowerDecomposition, LabelStyle::Latex));
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Plain direct-product power of the basis representation: no Pauli exclusion, no spin. Physical many-electron states are the Multiplets below.");
		}
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
		if (!ImGui::BeginTable("##group_character_table", columns,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame))
			return;
		ImGui::TableSetupColumn("");
		for (const std::string &label : table.classLabels)
			ImGui::TableSetupColumn("");
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		ImGui::TableSetColumnIndex(0);
		MathLabel(FormatIrrepLabel(table.pointGroupLabel, LabelStyle::Latex));
		for (std::size_t column = 0; column < table.classLabels.size(); ++column)
		{
			ImGui::TableSetColumnIndex(static_cast<int>(column + 1));
			MathLabel(FormatClassLabel(table.classLabels[column], LabelStyle::Latex));
		}
		for (std::size_t row = 0; row < table.irrepLabels.size(); ++row)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			MathLabel(FormatIrrepLabel(table.irrepLabels[row], LabelStyle::Latex));
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
			MathLabel("\\Gamma");
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
		ImGui::TextDisabled("Order starts as projection order (follows atom selection order); reorder freely.");
		const int columns = static_cast<int>(m_Result->reduction.siteLabels.size()) + 5;
		if (!ImGui::BeginTable(
				"##group_projected_vectors",
				columns,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame))
			return;
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("");
		for (const std::string &label : m_Result->reduction.siteLabels)
			ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("");
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		const char *headers[] = {"Irrep", "Copy", "Row"};
		for (int column = 0; column < 3; ++column)
		{
			ImGui::TableSetColumnIndex(column);
			ImGui::TextUnformatted(headers[column]);
		}
		for (std::size_t column = 0; column < m_Result->reduction.siteLabels.size(); ++column)
		{
			ImGui::TableSetColumnIndex(static_cast<int>(column + 3));
			ImGui::TextUnformatted(m_Result->reduction.siteLabels[column].c_str());
		}
		ImGui::TableSetColumnIndex(columns - 2);
		ImGui::TextUnformatted("Physical label");
		ImGui::TableSetColumnIndex(columns - 1);
		ImGui::TextUnformatted("Order");
		if (m_VectorOrder.size() != vectors.size()) { m_VectorOrder.resize(vectors.size()); std::iota(m_VectorOrder.begin(), m_VectorOrder.end(), 0); }
		for (std::size_t row = 0; row < m_VectorOrder.size(); ++row)
		{
			const std::size_t vectorIndex = m_VectorOrder[row];
			const SymmetryAdaptedVector &vector = vectors[vectorIndex];
			ImGui::PushID(static_cast<int>(vectorIndex));
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			MathLabel(FormatIrrepLabel(vector.irrepLabel, LabelStyle::Latex));
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", vector.occurrenceIndex + 1);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%d", vector.irrepRow + 1);
			for (std::size_t column = 0; column < vector.coefficients.size(); ++column)
			{
				ImGui::TableSetColumnIndex(static_cast<int>(column + 3));
				Cell(vector.coefficients[column]);
			}
			ImGui::TableSetColumnIndex(columns - 2);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##physical_label", m_PhysicalBuffers[vectorIndex].data(), m_PhysicalBuffers[vectorIndex].size());
			if (m_PhysicalBuffers[vectorIndex][0] != '\0')
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(assumption)");
			}
			ImGui::TableSetColumnIndex(columns - 1);
			ImGui::BeginDisabled(row == 0);
			if (ImGui::ArrowButton("##up", ImGuiDir_Up)) std::swap(m_VectorOrder[row], m_VectorOrder[row - 1]);
			ImGui::EndDisabled();
			ImGui::SameLine(0.0f, 2.0f);
			ImGui::BeginDisabled(row + 1 >= m_VectorOrder.size());
			if (ImGui::ArrowButton("##down", ImGuiDir_Down)) std::swap(m_VectorOrder[row], m_VectorOrder[row + 1]);
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	void GroupTheoryPanel::drawMultiplets()
	{
		if (m_Result->multiplets.empty())
			return;
		ImGui::TextUnformatted("Multiplets");
		if (ImGui::BeginTable("##group_multiplets", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableSetupColumn("");
			ImGui::TableSetupColumn("");
			ImGui::TableSetupColumn("");
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Term");
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted("Count per row");
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted("States");
			for (const MultipletTerm &term : m_Result->multiplets)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				MathLabel(FormatTermLabel(term.spinMultiplicity, term.irrepLabel, LabelStyle::Latex));
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
		const std::string tensorPower = m_Result->tensorPower > 0
			? "\n" + FormatTensorPower(m_Result->tensorPower, m_Result->tensorPowerDecomposition, style)
			: "";
		const std::string table = FormatCharacterTable(m_Result->characterTable, m_Result->reducibleCharacters, format);
		std::vector<std::string> labels;
		labels.reserve(m_PhysicalBuffers.size());
		for (const auto &buffer : m_PhysicalBuffers)
			labels.emplace_back(buffer.data());
		const std::string vectors = FormatProjectedVectors(m_Result->reduction, m_VectorOrder, labels, format);
		const std::string multiplets = FormatMultiplets(m_Result->multiplets, format);
		ImGui::SetClipboardText((decomposition + tensorPower + "\n\n" + table + "\n" + vectors + (m_Result->multiplets.empty() ? "" : "\n" + multiplets)).c_str());
	}
} // namespace DefectStudio
