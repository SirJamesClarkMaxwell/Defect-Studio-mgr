#include "Core/dspch.hpp"

#include "Presentation/Panels/GroupTheoryPanel.hpp"

#include <cstdio>
#include <numeric>
#include <string>

#include <imgui.h>

#include "Presentation/Panels/GroupTheoryFormatting.hpp"
#include "Domain/Symmetry/DirectProducts.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] float MathSegmentFontSize(const MathSegment &segment, float sizeScale)
		{
			return ImGui::GetStyle().FontSizeBase * sizeScale * (segment.level == 0 ? 1.0f : 0.7f);
		}

		void MathLabel(std::string_view latex, float sizeScale = 1.0f)
		{
			const float baseY = ImGui::GetCursorPosY();
			const float baseSize = ImGui::GetFontSize() * sizeScale;
			const float baseScreenY = ImGui::GetCursorScreenPos().y;
			ImFont *font = ImGui::GetFont();
			ImGui::BeginGroup();
			bool first = true;
			float overlineStart = 0.0f;
			float overlineEnd = 0.0f;
			bool hasOverline = false;
			const auto drawOverline = [&]() {
				if (hasOverline)
					ImGui::GetWindowDrawList()->AddLine(
						{overlineStart, baseScreenY + 0.08f * baseSize},
						{overlineEnd, baseScreenY + 0.08f * baseSize},
						ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
				hasOverline = false;
			};
			for (const MathSegment &segment : SplitMathSegments(latex))
			{
				if (!first)
					ImGui::SameLine(0.0f, 0.0f);
				if (!segment.overline)
					drawOverline();
				if (!segment.denominator.empty())
				{
					const float padding = 3.0f;
					const float fontSize = ImGui::GetStyle().FontSizeBase * sizeScale * 0.75f;
					ImGui::PushFont(font, fontSize);
					const float numeratorWidth = ImGui::CalcTextSize(segment.text.c_str()).x;
					const float denominatorWidth = ImGui::CalcTextSize(segment.denominator.c_str()).x;
					ImGui::PopFont();
					const float width = std::max(numeratorWidth, denominatorWidth) + padding * 2.0f;
					const float startX = ImGui::GetCursorPosX();
					const float startScreenX = ImGui::GetCursorScreenPos().x;
					ImGui::SetCursorPosY(baseY - 0.25f * baseSize);
					ImGui::PushFont(font, fontSize);
					ImGui::SetCursorPosX(startX + (width - numeratorWidth) * 0.5f);
					ImGui::TextUnformatted(segment.text.c_str());
					ImGui::PopFont();
					const float lineY = ImGui::GetItemRectMax().y + 1.0f;
					ImGui::SetCursorPosY(baseY + 0.42f * baseSize);
					ImGui::PushFont(font, fontSize);
					ImGui::SetCursorPosX(startX + (width - denominatorWidth) * 0.5f);
					ImGui::TextUnformatted(segment.denominator.c_str());
					ImGui::PopFont();
					ImGui::GetWindowDrawList()->AddLine(
						{startScreenX, lineY},
						{startScreenX + width, lineY},
						ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
					ImGui::SetCursorPosX(startX);
					ImGui::SetCursorPosY(baseY + baseSize);
					ImGui::Dummy({width, baseSize * 0.5f});
					if (segment.overline)
					{
						if (!hasOverline)
							overlineStart = ImGui::GetItemRectMin().x;
						overlineEnd = ImGui::GetItemRectMax().x;
						hasOverline = true;
					}
				}
				else
				{
					ImGui::SetCursorPosY(
						baseY + (segment.level > 0 ? -0.1f : segment.level < 0 ? 0.4f : 0.0f) * baseSize);
					const bool pushed = sizeScale != 1.0f || segment.level != 0;
					if (pushed)
						ImGui::PushFont(font, MathSegmentFontSize(segment, sizeScale));
					ImGui::TextUnformatted(segment.text.c_str());
					if (segment.overline)
					{
						if (!hasOverline)
							overlineStart = ImGui::GetItemRectMin().x;
						overlineEnd = ImGui::GetItemRectMax().x;
						hasOverline = true;
					}
					if (pushed)
						ImGui::PopFont();
				}
				first = false;
			}
			drawOverline();
			ImGui::SetCursorPosY(baseY);
			ImGui::EndGroup();
		}

		[[nodiscard]] float MathLabelWidth(std::string_view latex, float sizeScale)
		{
			float width = 0.0f;
			for (const MathSegment &segment : SplitMathSegments(latex))
			{
				const bool pushed = sizeScale != 1.0f || segment.level != 0;
				if (pushed)
					ImGui::PushFont(ImGui::GetFont(), MathSegmentFontSize(segment, sizeScale));
				if (segment.denominator.empty())
					width += ImGui::CalcTextSize(segment.text.c_str()).x;
				else
				{
					const float numeratorWidth = ImGui::CalcTextSize(segment.text.c_str()).x;
					const float denominatorWidth = ImGui::CalcTextSize(segment.denominator.c_str()).x;
					width += std::max(numeratorWidth, denominatorWidth) + 6.0f;
				}
				if (pushed)
					ImGui::PopFont();
			}
			return width;
		}

		void CenteredMathLabel(std::string_view latex, float sizeScale)
		{
			const float width = MathLabelWidth(latex, sizeScale);
			const float available = ImGui::GetContentRegionAvail().x;
			const float cursor = ImGui::GetCursorPosX();
			ImGui::SetCursorPosX(cursor + std::max(0.0f, (available - width) * 0.5f));
			MathLabel(latex, sizeScale);
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
		const std::string pointGroup = FormatIrrepLabel(m_Result->detection.pointGroupLabel, LabelStyle::Latex);
		const std::string decomposition = FormatDecomposition(m_Result->reduction.decomposition, LabelStyle::Latex);
		std::string header = pointGroup + "   " + decomposition;
		if (m_Result->tensorPower > 0)
			header += "   " + FormatTensorPower(
				m_Result->tensorPower, m_Result->tensorPowerDecomposition, LabelStyle::Latex);
		CenteredMathLabel(header, 1.5f);
		if (m_Result->tensorPower > 0 && ImGui::IsItemHovered())
			ImGui::SetTooltip("Plain direct-product power of the basis representation: no Pauli exclusion, no spin. Physical many-electron states are the Multiplets below.");
		std::string detectionInfo;
		if (!m_Result->detection.ran)
			detectionInfo = "(manual)";
		else
		{
			char tolerance[128];
			std::snprintf(
				tolerance, sizeof(tolerance), "(detected, pymatgen %s, tol %.3g Å)",
				m_Result->detection.detectorSymbol.c_str(), m_Result->detection.tolerance);
			detectionInfo = tolerance;
		}
		const float infoWidth = ImGui::CalcTextSize(detectionInfo.c_str()).x;
		ImGui::SetCursorPosX(
			ImGui::GetCursorPosX() +
			std::max(0.0f, (ImGui::GetContentRegionAvail().x - infoWidth) * 0.5f));
		ImGui::TextDisabled("%s", detectionInfo.c_str());
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
		for (std::size_t index = 0; index < table.classLabels.size(); ++index)
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
		const int columns = static_cast<int>(m_Result->reduction.siteLabels.size()) + 6;
		if (!ImGui::BeginTable(
				"##group_projected_vectors",
				columns,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame))
			return;
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("");
		for (std::size_t index = 0; index < m_Result->reduction.siteLabels.size(); ++index)
			ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("");
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		const char *headers[] = {"Active", "Irrep", "Copy", "Row"};
		for (int column = 0; column < 4; ++column)
		{
			ImGui::TableSetColumnIndex(column);
			ImGui::TextUnformatted(headers[column]);
		}
		for (std::size_t column = 0; column < m_Result->reduction.siteLabels.size(); ++column)
		{
			ImGui::TableSetColumnIndex(static_cast<int>(column + 4));
			ImGui::TextUnformatted(m_Result->reduction.siteLabels[column].c_str());
		}
		ImGui::TableSetColumnIndex(columns - 2);
		ImGui::TextUnformatted("Physical label");
		ImGui::TableSetColumnIndex(columns - 1);
		ImGui::TextUnformatted("Order");
		if (m_VectorOrder.size() != vectors.size())
		{
			m_VectorOrder.resize(vectors.size());
			std::iota(m_VectorOrder.begin(), m_VectorOrder.end(), 0);
		}
		for (std::size_t row = 0; row < m_VectorOrder.size(); ++row)
		{
			const std::size_t vectorIndex = m_VectorOrder[row];
			const SymmetryAdaptedVector &vector = vectors[vectorIndex];
			ImGui::PushID(static_cast<int>(vectorIndex));
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			bool firstCopyRow = row == 0 || vectors[m_VectorOrder[row - 1]].irrepLabel != vector.irrepLabel || vectors[m_VectorOrder[row - 1]].occurrenceIndex != vector.occurrenceIndex;
			bool active = vectorIndex < m_ActiveVectors.size() && m_ActiveVectors[vectorIndex] != 0;
			if (firstCopyRow)
			{
				if (ImGui::Checkbox("##active", &active))
					for (std::size_t index = 0; index < vectors.size(); ++index)
						if (vectors[index].irrepLabel == vector.irrepLabel &&
							vectors[index].occurrenceIndex == vector.occurrenceIndex &&
							index < m_ActiveVectors.size())
							m_ActiveVectors[index] = active ? 1 : 0;
			}
			else
			{
				ImGui::BeginDisabled();
				ImGui::Checkbox("##active", &active);
				ImGui::EndDisabled();
			}
			ImGui::TableSetColumnIndex(1);
			MathLabel(FormatIrrepLabel(vector.irrepLabel, LabelStyle::Latex));
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%d", vector.occurrenceIndex + 1);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%d", vector.irrepRow + 1);
			for (std::size_t column = 0; column < vector.coefficients.size(); ++column)
			{
				ImGui::TableSetColumnIndex(static_cast<int>(column + 4));
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
			if (ImGui::ArrowButton("##up", ImGuiDir_Up))
				std::swap(m_VectorOrder[row], m_VectorOrder[row - 1]);
			ImGui::EndDisabled();
			ImGui::SameLine(0.0f, 2.0f);
			ImGui::BeginDisabled(row + 1 >= m_VectorOrder.size());
			if (ImGui::ArrowButton("##down", ImGuiDir_Down))
				std::swap(m_VectorOrder[row], m_VectorOrder[row + 1]);
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
			for (std::size_t termIndex = 0; termIndex < m_Result->multiplets.size(); ++termIndex)
			{
				const MultipletTerm &term = m_Result->multiplets[termIndex];
				ImGui::TableNextRow();
				ImGui::PushID(static_cast<int>(termIndex));
				ImGui::TableSetColumnIndex(0);
				const bool selected = m_SelectedTerm.has_value() &&
					m_SelectedTerm->first == term.irrepLabel &&
					m_SelectedTerm->second == term.spinMultiplicity;
				if (selected)
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header, 0.35f));
				const ImVec2 labelPos = ImGui::GetCursorPos();
				ImGui::SetNextItemAllowOverlap();
				if (ImGui::Selectable(
						"##term", selected, ImGuiSelectableFlags_SpanAllColumns,
						{0.0f, ImGui::GetTextLineHeightWithSpacing()}))
					m_SelectedTerm = std::make_pair(term.irrepLabel, term.spinMultiplicity);
				ImGui::SetCursorPos(labelPos);
				MathLabel(FormatTermLabel(term.spinMultiplicity, term.irrepLabel, LabelStyle::Latex));
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%d", term.countPerRow);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%d", term.totalStates);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::Text("Total states: %d", m_Result->multipletTotalStates);
		if (m_SelectedTerm.has_value())
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Wavefunctions of ");
			ImGui::SameLine(0.0f, 0.0f);
			MathLabel(FormatTermLabel(m_SelectedTerm->second, m_SelectedTerm->first, LabelStyle::Latex));
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Phase convention: first coefficient positive per state; partners are not phase-linked.");
			std::string previous;
			for (const MultipletWavefunction &state : m_Result->wavefunctions)
				if (state.irrepLabel == m_SelectedTerm->first && state.spinMultiplicity == m_SelectedTerm->second)
				{
					const std::string configuration = FormatConfiguration(m_Result->activeShells, state.configuration);
					if (configuration != previous)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
						MathLabel(configuration, 1.25f);
						ImGui::PopStyleColor();
					}
					MathLabel(
						FormatWavefunction(state, m_Result->activeOrbitalLabels, LabelStyle::Latex), 1.25f);
					previous = configuration;
				}
			if (!m_Result->wavefunctionsSkippedReason.empty())
				ImGui::TextDisabled("%s", m_Result->wavefunctionsSkippedReason.c_str());
			if (ImGui::Button("Copy term as LaTeX"))
				ImGui::SetClipboardText(
					FormatTermWavefunctions(*m_Result, m_SelectedTerm->first, m_SelectedTerm->second).c_str());
		}
	}

	void GroupTheoryPanel::drawDirectProducts()
	{
		ImGui::TextDisabled("Direct products");
		const int columns = static_cast<int>(m_Result->characterTable.irrepLabels.size()) + 1;
		const ImGuiTableFlags flags =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame;
		if (ImGui::BeginTable("##group_direct_products", columns, flags))
		{
			for (std::size_t i = 0; i <= m_Result->characterTable.irrepLabels.size(); ++i)
				ImGui::TableSetupColumn("");
			for (std::size_t row = 0; row <= m_Result->characterTable.irrepLabels.size(); ++row)
			{
				ImGui::TableNextRow();
				for (std::size_t column = 0; column <= m_Result->characterTable.irrepLabels.size(); ++column)
				{
					ImGui::TableSetColumnIndex(static_cast<int>(column));
					if (row == 0 && column == 0)
						MathLabel(FormatIrrepLabel(m_Result->characterTable.pointGroupLabel, LabelStyle::Latex) +
							" \\otimes " + FormatIrrepLabel(m_Result->characterTable.pointGroupLabel, LabelStyle::Latex));
					else if (row == 0)
						MathLabel(FormatIrrepLabel(m_Result->characterTable.irrepLabels[column - 1], LabelStyle::Latex));
					else if (column == 0)
						MathLabel(FormatIrrepLabel(m_Result->characterTable.irrepLabels[row - 1], LabelStyle::Latex));
					else
						MathLabel(FormatIrrepSum(m_DirectProducts[row - 1][column - 1], LabelStyle::Latex));
				}
			}
			ImGui::EndTable();
		}
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
		const std::string directProducts = m_DirectProducts.empty() ? "" :
			"\n" + FormatDirectProductTable(m_Result->characterTable, m_DirectProducts, format);
		ImGui::SetClipboardText((decomposition + tensorPower + "\n\n" + table + "\n" + vectors + (m_Result->multiplets.empty() ? "" : "\n" + multiplets) + directProducts).c_str());
	}
} // namespace DefectStudio
