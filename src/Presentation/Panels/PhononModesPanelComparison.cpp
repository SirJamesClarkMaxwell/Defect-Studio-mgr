#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <imgui.h>
#include "Presentation/Panels/PlotWidgets.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr std::array<std::string_view, 17> Columns = {"porównaj", "Przypadek", "Zestaw", "ZPL [eV]",
			"ΔQ [amu^½ Å]", "S", "DW [%]", "ℏω_eff [meV]", "Najsilniejszy mod [meV]", "S_k", "n_eff", "Mody urojone",
			"ΔQ (CCD) [amu^½ Å]", "max |ΔR| [Å]", "atomy ΔQ²", "pokrycie ΔQ² modami [%]", "największy udział modu [%]"};

	}

	void PhononModesPanel::drawComparison()
	{
		const auto rows = BuildPhononComparisonRows(*m_Session, m_Session->m_ProjectDirectory);
		const auto columns = PhononComparisonColumns(rows, m_Session->Settings().carriedFraction);
		ImGui::PushID("comparison");
		TableExportPopup(columns, m_Session->m_ProjectDirectory, "comparison", m_ComparisonExport, "Eksport TSV…");
		ImGui::SameLine();
		ImGui::BeginDisabled(rows.empty());
		if (ImGui::SmallButton("Kopiuj"))
		{
			DataTableFormat format; format.originHeader = m_ComparisonExport.originHeader;
			ImGui::SetClipboardText(FormatDataTable(columns, format).c_str());
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", rows.empty() ? "Brak zestawów fononowych do porównania." : "Kopiuj tabelę TSV z bieżącym ustawieniem nagłówka Origin.");
		std::optional<std::size_t> selected;
		const auto carriedHeader = std::format("atomy {:g} % ΔQ²", 100.0 * m_Session->Settings().carriedFraction);
		if (ImGui::BeginTable("##comparison", static_cast<int>(Columns.size()), ImGuiTableFlags_ScrollX |
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit,
			ImVec2(0.0f, std::max(150.0f, ImGui::GetContentRegionAvail().y * 0.35f))))
		{
			for (std::size_t i = 0; i < Columns.size(); ++i)
				ImGui::TableSetupColumn(i == 14 ? carriedHeader.c_str() : Columns[i].data());
			ImGui::TableSetupScrollFreeze(3, 1);
			ImGui::TableHeadersRow();
			std::optional<std::string> group;
			for (std::size_t index = 0; index < rows.size(); ++index)
			{
				const auto &row = rows[index];
				if (!group || *group != row.caseId)
				{
					group = row.caseId;
					ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(row.caseId.empty() ? "Bez przypadku" : row.caseName.c_str());
				}
				ImGui::PushID(row.directory.Utf8().c_str());
				ImGui::TableNextRow();
				if (row.active)
				{
					const auto accent = ImGui::GetColorU32(ImGuiCol_CheckMark, 0.35f);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, accent);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, accent);
				}
				ImGui::TableSetColumnIndex(0);
				bool compared = row.compared;
				const bool known = std::any_of(m_Session->Datasets().begin(), m_Session->Datasets().end(), [&](const auto &dataset) {
					return PhononPanelSession::SameFilePath(dataset.directory, row.directory, m_Session->m_ProjectDirectory);
				});
				ImGui::BeginDisabled(!known);
				if (ImGui::Checkbox("##compared", &compared)) m_Session->SetCompared(row.directory, compared);
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("%s", known ? "Uwzględnij zestaw we wspólnym porównaniu wykresów." : "Brak rozpoznanego zestawu; wczytaj podsumowanie lub przeskanuj ponownie.");
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(row.caseName.c_str());
				ImGui::TableSetColumnIndex(2);
				if (ImGui::Selectable(row.label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns |
					ImGuiSelectableFlags_AllowOverlap)) selected = index;
				const auto path = PhononPanelSession::DirectoryLabel(row.directory, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots);
				ImGui::SetItemTooltip("%s\nKliknij, aby aktywować przypadek i wczytać zestaw.", path.c_str());
				for (std::size_t i = 0; i < row.values.size(); ++i)
				{
					ImGui::TableSetColumnIndex(static_cast<int>(i + 3));
					if (row.values[i]) ImGui::Text("%.6g", *row.values[i]);
					else ImGui::TextUnformatted("-");
					if (!row.values[i]) ImGui::SetItemTooltip("%s", row.reasons[i].c_str());
					else if (i == 0) ImGui::SetItemTooltip("ZPL: %s", row.zplSource.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (rows.empty()) ImGui::TextDisabled("Przypnij zestawy fononów do przypadków lub zaznacz je do porównania.");
		ImGui::TextUnformatted("Podobieństwo ΔR");
		ImGui::SetItemTooltip("Wymagana ta sama kolejność atomów i pierwiastków oraz masy zgodne do 1e-3 amu. Bez dopasowania ani obrotu atomów.");
		const auto overlap = BuildDisplacementOverlapColumns(*m_Session, m_Session->m_ProjectDirectory);
		ImGui::PushID("overlap");
		TableExportPopup(overlap, m_Session->m_ProjectDirectory, "displacement_overlap",
			m_PlotExports["displacement_overlap"], "Eksport TSV…");
		for (std::size_t first = 1; first < overlap.size(); first += IMGUI_TABLE_MAX_COLUMNS - 2)
		{
			const auto end = std::min(overlap.size(), first + IMGUI_TABLE_MAX_COLUMNS - 2);
			ImGui::PushID(static_cast<int>(first));
			if (ImGui::BeginTable("##overlap", static_cast<int>(end - first + 1),
				ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit,
				ImVec2(0.0f, std::min(180.0f, (overlap.size() + 1) * 1.8f * ImGui::GetFontSize()))))
			{
				ImGui::TableSetupColumn(overlap[0].name.c_str());
				for (std::size_t column = first; column < end; ++column) ImGui::TableSetupColumn(overlap[column].name.c_str());
				ImGui::TableSetupScrollFreeze(1, 1);
				ImGui::TableHeadersRow();
				for (std::size_t row = 0; row < overlap[0].text.size(); ++row)
				{
					ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(overlap[0].text[row].c_str());
					for (std::size_t column = first; column < end; ++column)
					{
						ImGui::TableSetColumnIndex(static_cast<int>(column - first + 1));
						const double value = overlap[column].values[row];
						if (std::isfinite(value)) ImGui::Text("%.3f", value);
						else
						{
							ImGui::TextUnformatted("—");
							ImGui::SetItemTooltip("Różna liczba atomów lub kolejność pierwiastków. Wymagane zgodne masy i niezerowe ΔQ; ta sama kolejność atomów, bez dopasowania ani obrotu.");
						}
					}
				}
				ImGui::EndTable();
			}
			ImGui::PopID();
		}
		ImGui::PopID();
		ImGui::TextUnformatted("Lokalizacja ΔR");
		auto plot = BuildLocalisationPlotData(*m_Session, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots, CurrentPlotColors());
		const auto label = PhononExportLabel(*m_Session, "comparison");
		PlotExportPopup(plot, m_Session->m_ProjectDirectory, label, m_PlotExports[plot.id]);
		drawFigure(plot, label);
		const bool reset = m_LastPlotRevision != m_Session->PlotRevision() || m_PlotTab != 6;
		if (plot.series.empty()) ImGui::TextDisabled("Brak zestawu CCD z ΔR w aktywnym lub porównywanych przypadkach.");
		else if (ImPlot::BeginPlot("##localisation", ImVec2(-1.0f, std::max(160.0f, ImGui::GetContentRegionAvail().y))))
		{
			ImPlot::SetupAxes(" ", " ");
			ImPlot::SetupAxisLimits(ImAxis_X1, plot.x.minimum, plot.x.maximum, reset ? ImPlotCond_Always : ImPlotCond_Once);
			ImPlot::SetupAxisLimits(ImAxis_Y1, plot.y.minimum, plot.y.maximum, ImPlotCond_Once);
			ImPlot::SetupFinish();
			ImRect plotRect;
			const ImRect frame = CurrentPlotFrame(&plotRect);
			for (auto &series : plot.series)
			{
				auto &style = m_Session->Settings().seriesStyles[PhononPlotStyleKey(series)];
				const auto legendId = PhononPlotLegendId(series);
				ResolvePlotItemColor(series, style, legendId);
				if (PlotSeries(series, style, legendId)) persist();
				ResolvePlotItemColor(series, style, legendId);
			}
			ImPlot::EndPlot();
			m_LastPlotRevision = m_Session->PlotRevision(); m_PlotTab = 6;
			DrawTexAxisLabels(frame, plotRect, "r [Å]", plot.y.labelTex.c_str());
		}
		if (selected)
		{
			const auto &row = rows[*selected];
			if (!row.caseId.empty()) (void)m_Session->ActivateCase(row.caseId);
			if (PhononPanelSession::SameFilePath(row.directory, m_Session->Settings().selectedCcdDirectory, m_Session->m_ProjectDirectory))
				(void)m_Session->loadCcdResult(row.directory);
			else m_Session->loadResult(row.directory);
			persist();
		}
		ImGui::PopID();
	}
}
