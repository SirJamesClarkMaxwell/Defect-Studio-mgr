#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <imgui.h>

namespace DefectStudio
{
	namespace
	{
		constexpr std::array<std::string_view, 12> Columns = {"porównaj", "Przypadek", "Zestaw", "ZPL [eV]",
			"ΔQ [amu^½ Å]", "S", "DW [%]", "ℏω_eff [meV]", "Najsilniejszy mod [meV]", "S_k", "n_eff", "Mody urojone"};

		void FillComparison(PhononComparisonRow &row, const PhononAnalysisResult &result, std::string_view preferred,
			bool hasModes, const std::string &modesError)
		{
			const auto set = [&](std::size_t column, double value) {
				if (std::isfinite(value)) row.values[column] = value;
				else row.reasons[column] = "Wartość nie jest skończona.";
			};
			auto selected = SelectPhononPlCurve(result, preferred);
			if (PhononPanelSession::PlCurve(result, selected.choice).empty())
			{
				// Without PL data, the summary can still supply the requested ZPL or a recorded fallback.
				for (const auto key : {std::string(preferred), std::string("computed"), std::string("spin_purified"),
					std::string("experimental"), std::string("formula")})
				{
					const auto value = key == "formula" ? result.zplEv.formula : key == "experimental" ? result.zplEv.experimental :
						key == "spin_purified" ? result.zplEv.spinPurified : result.zplEv.computed;
					if (value && std::isfinite(*value)) { selected = {key, value}; break; }
				}
			}
			row.values[0] = selected.zplEv;
			row.zplSource = selected.zplEv ? PhononZplSourceLabel(selected.choice) : "Brak dostępnego ZPL w podsumowaniu.";
			if (!selected.zplEv) row.reasons[0] = row.zplSource;
			set(1, result.deltaQAmuHalfAngstrom); set(2, result.sTotal); set(3, 100.0 * result.debyeWaller);
			if (!hasModes)
			{
				for (std::size_t i = 4; i < row.values.size(); ++i)
					row.reasons[i] = modesError.empty() ? "Brak tabeli modów." : modesError;
				return;
			}
			set(8, static_cast<double>(result.imaginaryModes.size()));
			std::optional<std::size_t> strongest;
			double q2 = 0.0, weighted = 0.0;
			bool valid = true;
			for (std::size_t i = 0; i < result.modes.size(); ++i)
			{
				const auto &mode = result.modes[i];
				if (mode.isTranslation) continue;
				if (std::isfinite(mode.huangRhys) && (!strongest || mode.huangRhys > result.modes[*strongest].huangRhys)) strongest = i;
				valid = valid && std::isfinite(mode.frequencyMeV) && mode.frequencyMeV > 0.0 && std::isfinite(mode.qAmuHalfAngstrom);
				q2 += mode.qAmuHalfAngstrom * mode.qAmuHalfAngstrom;
				const double product = mode.frequencyMeV * mode.qAmuHalfAngstrom;
				weighted += product * product;
			}
			// scripts/python/examples/ccd_analysis.py:254-271 (phonon_mode), q_k^2-weighted RMS frequency.
			if (valid && strongest && q2 > 0.0 && std::isfinite(q2) && std::isfinite(weighted)) set(4, std::sqrt(weighted / q2));
			else row.reasons[4] = "Potrzebne skończone dodatnie częstotliwości nietranslacyjne i niezerowe przesunięcie q_k.";
			if (strongest)
			{
				const auto &mode = result.modes[*strongest];
				set(5, mode.frequencyMeV); set(6, mode.huangRhys); set(7, mode.effectiveAtomCount);
			}
			else
				for (std::size_t i = 5; i < 8; ++i) row.reasons[i] = "Brak modów nietranslacyjnych z dostępnym S_k.";
		}
	}

	std::vector<PhononComparisonRow> BuildPhononComparisonRows(PhononPanelSession &session, const Path &project)
	{
		std::vector<PhononComparisonRow> rows;
		const auto add = [&](const Path &directory, const std::string &caseId, const std::string &caseName,
			const std::string &label, std::string_view preferred) {
			PhononComparisonRow row;
			row.directory = directory; row.caseId = caseId; row.caseName = caseName; row.label = label.empty() ? directory.filename().Utf8() : label;
			row.compared = session.IsCompared(directory);
			row.active = !caseId.empty() && caseId == session.ActiveCaseId() && session.Result() &&
				PhononPanelSession::SameFilePath(directory, session.Settings().resultDirectory, project);
			row.reasons.fill("Brak wartości w podsumowaniu.");
			std::string error;
			std::optional<std::reference_wrapper<const PhononAnalysisResult>> result;
			if (session.Result() && PhononPanelSession::SameFilePath(directory, session.Settings().resultDirectory, project))
				result = std::cref(*session.Result());
			else result = session.PlotModes(directory, error);
			const bool hasModes = result && !result->get().modes.empty();
			if (!result)
			{
				std::string summaryError;
				result = session.Spectra(directory, summaryError);
				if (!result) row.reasons.fill(summaryError.empty() ? "Brak podsumowania zestawu." : summaryError);
			}
			if (result) FillComparison(row, result->get(), preferred, hasModes, error);
			rows.push_back(std::move(row));
		};
		const auto &cases = session.Cases();
		for (bool active : {true, false})
			for (const auto &item : cases)
				if ((item.id == session.ActiveCaseId()) == active)
					for (const auto &path : item.datasets)
					{
						const auto found = std::find_if(session.Datasets().begin(), session.Datasets().end(), [&](const auto &dataset) {
							return PhononPanelSession::SameFilePath(path, dataset.directory, project);
						});
						if (found != session.Datasets().end() && found->kind == AnalysisDatasetKind::Phonons)
							add(found->directory, item.id, item.name, found->label, item.settings.plZpl);
						else if (found == session.Datasets().end() && PhononPanelSession::SameFilePath(path, item.settings.resultDirectory, project))
							add(path, item.id, item.name, path.filename().Utf8(), item.settings.plZpl);
					}
		for (const auto &dataset : session.Datasets())
			if (dataset.kind == AnalysisDatasetKind::Phonons && session.IsCompared(dataset.directory) && session.DatasetCaseId(dataset.directory).empty())
				add(dataset.directory, {}, "-", dataset.label, session.Settings().plZpl);
		return rows;
	}

	std::vector<DataTableColumn> PhononComparisonColumns(std::span<const PhononComparisonRow> rows)
	{
		if (rows.empty()) return {};
		std::vector<DataTableColumn> columns = {{"porównaj", "", "", {}, {}}, {"Przypadek", "", "", {}, {}},
			{"Zestaw", "", "", {}, {}}, {"ZPL", "eV", "", {}, {}}, {"ΔQ", "amu^½ Å", "", {}, {}},
			{"S", "", "", {}, {}}, {"DW", "%", "", {}, {}}, {"ℏω_eff", "meV", "", {}, {}},
			{"Najsilniejszy mod", "meV", "", {}, {}}, {"S_k", "", "", {}, {}}, {"n_eff", "", "", {}, {}},
			{"Mody urojone", "", "", {}, {}}};
		for (const auto &row : rows)
		{
			columns[0].values.push_back(row.compared ? 1.0 : 0.0);
			columns[1].text.push_back(row.caseName); columns[2].text.push_back(row.label);
			for (std::size_t i = 0; i < row.values.size(); ++i)
				columns[i + 3].values.push_back(row.values[i].value_or(std::numeric_limits<double>::quiet_NaN()));
		}
		return columns;
	}

	void PhononModesPanel::drawComparison()
	{
		const auto rows = BuildPhononComparisonRows(*m_Session, m_Session->m_ProjectDirectory);
		const auto columns = PhononComparisonColumns(rows);
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
		if (ImGui::BeginTable("##comparison", static_cast<int>(Columns.size()), ImGuiTableFlags_ScrollX |
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit))
		{
			for (const auto label : Columns) ImGui::TableSetupColumn(label.data());
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
		if (selected)
		{
			const auto &row = rows[*selected];
			if (!row.caseId.empty()) (void)m_Session->ActivateCase(row.caseId);
			m_Session->loadResult(row.directory);
			persist();
		}
		ImGui::PopID();
	}
}
