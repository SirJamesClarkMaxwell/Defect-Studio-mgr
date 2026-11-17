#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <imgui.h>
#include <implot.h>

namespace DefectStudio
{
	void PhononModesPanel::drawConvergence()
	{
		const auto &project = m_Session->m_ProjectDirectory;
		auto &settings = m_Session->Settings();
		std::string error;
		const auto series = BuildPhononSeries(*m_Session, project, error);
		if (series.empty())
		{
			ImGui::TextDisabled("Brak zestawów fononowych w aktywnym, widocznym przypadku.");
			ImGui::SetItemTooltip("Dodaj lub zaimportuj zestawy o różnych rozmiarach superkomórki do tego samego przypadku.");
			if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
			return;
		}
		const auto context = project.Utf8() + "/" + m_Session->ActiveCaseId();
		if (m_ConvergenceContext != context) { m_ConvergenceContext = context; m_ConvergenceSeries.clear(); }
		auto selected = std::find_if(series.begin(), series.end(), [&](const auto &item) { return item.id == m_ConvergenceSeries; });
		if (selected == series.end()) { selected = series.begin(); m_ConvergenceSeries = selected->id; }
		ImGui::TextUnformatted("Seria"); ImGui::SameLine(); ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##convergenceSeries", selected->label.c_str()))
		{
			for (const auto &item : series)
				if (ImGui::Selectable((item.label + std::format(" — {} zestawów###", item.members.size()) + item.id).c_str(), item.id == selected->id))
					m_ConvergenceSeries = item.id;
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Metoda HR, cutoff, σ, σ_max, γ i źródło muszą być identyczne. Domyślnie najliczniejsza seria.");
		selected = std::find_if(series.begin(), series.end(), [&](const auto &item) { return item.id == m_ConvergenceSeries; });
		const auto &group = *selected;
		if (group.members.front().result.get().supercell == std::nullopt)
		{
			ImGui::TextDisabled("komórka FC: n nieznane, pierwszy punkt przy n = 0 (w eksporcie n puste).");
			ImGui::SetItemTooltip("Liczba atomów komórki FC nie określa mnożników superkomórki.");
		}
		if (std::any_of(group.members.begin(), group.members.end(), [](const auto &item) { return item.uncertainIrreps; }))
		{
			ImGui::TextWrapped("Klasyfikacja irrepów niepewna dla dużych komórek (tolerancja projekcji).");
			ImGui::SetItemTooltip("Udział ??? przekracza 10%% całkowitego S w co najmniej jednym zestawie.");
		}
		if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
		ImGui::TextUnformatted("Skalar"); ImGui::SameLine(); ImGui::SetNextItemWidth(150.0f);
		constexpr std::array labels = {"S", "DW", "ℏω_eff [meV]", "E_FC [meV]"};
		if (ImGui::BeginCombo("##convergenceScalar", labels[m_ConvergenceScalar]))
		{
			for (int i = 0; i < static_cast<int>(labels.size()); ++i)
				if (ImGui::Selectable(labels[i], m_ConvergenceScalar == i)) m_ConvergenceScalar = i;
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("S i DW z podsumowania; ℏω_eff = Σ S_k ℏω_k / S; E_FC = Σ S_k ℏω_k, tylko ω > 0.");
		ImGui::SameLine(); ImGui::TextUnformatted("Oś PL"); ImGui::SameLine(); ImGui::SetNextItemWidth(140.0f);
		if (ImGui::BeginCombo("##seriesAxis", settings.plAxis == "zpl_loss" ? "E_ZPL − E [meV]" : "E [eV]"))
		{
			for (const auto &[key, label] : {std::pair{"energy", "E [eV]"}, std::pair{"zpl_loss", "E_ZPL − E [meV]"}})
				if (ImGui::Selectable(label, settings.plAxis == key)) { settings.plAxis = key; m_Session->InvalidatePlots(); persist(); }
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Ta sama oś co w zakładce PL; każda krzywa używa własnego ZPL.");
		ImGui::SameLine(); ImGui::TextUnformatted("Normalizacja"); ImGui::SameLine(); ImGui::SetNextItemWidth(100.0f);
		if (ImGui::BeginCombo("##seriesNormalization", settings.plNormalization == "max" ? "max = 1" : settings.plNormalization == "area" ? "pole = 1" : "brak"))
		{
			for (const auto &[key, label] : {std::pair{"none", "brak"}, std::pair{"max", "max = 1"}, std::pair{"area", "pole = 1"}})
				if (ImGui::Selectable(label, settings.plNormalization == key)) { settings.plNormalization = key; m_Session->InvalidatePlots(); persist(); }
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Wspólne ustawienie z zakładką PL; normalizacja na pełnej siatce. Eksport Origin zachowuje surowe L [1/eV].");
		ImGui::TextUnformatted("ZPL"); ImGui::SameLine(); ImGui::SetNextItemWidth(180.0f);
		if (ImGui::BeginCombo("##seriesZpl", PhononZplSourceLabel(settings.plZpl).c_str()))
		{
			for (const auto key : {"computed", "spin_purified", "experimental", "formula"})
				if (ImGui::Selectable(PhononZplSourceLabel(key).c_str(), settings.plZpl == key)) m_Session->SetPlZpl(key);
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Wybór wspólny z PL; przy braku wybranego ZPL używany jest dostępny wariant, jak w PL.");
		ImGui::SameLine();
		auto &exportState = m_PlotExports["convergenceOrigin/" + context + "/" + group.id];
		const auto firstZpl = SelectPhononPlCurve(group.members.front().result.get(), settings.plZpl);
		const bool noGrid = !firstZpl.zplEv || group.members.front().result.get().plPhotonEnergyEv.empty();
		ImGui::BeginDisabled(project.Empty() || noGrid);
		if (ImGui::Button("Eksport serii (Origin)…")) ImGui::OpenPopup("##seriesOrigin");
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", project.Empty() ? "Otwórz projekt, aby zapisać do exports/." : noGrid ?
				"Pierwszy zestaw musi mieć siatkę PL i ZPL." : "Jeden plik Origin, trzy wiersze nagłówka, wszystkie rozmiary; istniejące pliki dostają kolejne sufiksy.");
		if (ImGui::BeginPopup("##seriesOrigin"))
		{
			ImGui::TextWrapped("exports/%s", PhononSeriesFileName(group).c_str());
			ImGui::TextWrapped("Siatka E−E_ZPL pierwszego zestawu; interpolacja na ZPL każdego zestawu. Surowe L i S(hw).");
			if (ImGui::Button("Zapisz w exports/"))
			{
				if (SavePhononSeriesOrigin(group, settings.plZpl, project, exportState.file, exportState.message))
					exportState.message = "Zapisano " + PlotSourceComment(exportState.file, project);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemTooltip("Nagłówek Origin jest zawsze włączony; bez nadpisywania, sufiksy _2, _3, …");
			ImGui::EndPopup();
		}
		if (!exportState.message.empty()) ImGui::TextWrapped("%s", exportState.message.c_str());

		const auto start = ImGui::GetCursorScreenPos();
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x), height = 8.0f;
		for (std::size_t i = 0; i < 32; ++i)
		{
			const auto a = PhononSeriesColor(i, 33), b = PhononSeriesColor(i + 1, 33);
			const auto ca = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, a.w));
			const auto cb = ImGui::ColorConvertFloat4ToU32(ImVec4(b.x, b.y, b.z, b.w));
			ImGui::GetWindowDrawList()->AddRectFilledMultiColor({start.x + width * i / 32.0f, start.y},
				{start.x + width * (i + 1) / 32.0f, start.y + height}, ca, cb, cb, ca);
		}
		ImGui::Dummy({width, height});
		ImGui::SetItemTooltip("Kolor rośnie z n: pierwszy → środkowy → ostatni zestaw. Prawy klik legendy zmienia styl i widoczność.");
		ImGui::Text("%s  →  %s  →  %s", group.members.front().label.c_str(),
			group.members[group.members.size() / 2].label.c_str(), group.members.back().label.c_str());

		const std::array kinds = {m_ConvergenceScalar, 4, 5, 6};
		const float plotHeight = std::max(200.0f, (ImGui::GetContentRegionAvail().y - 60.0f) * 0.5f);
		if (ImGui::BeginTable("##seriesPlots", 2, ImGuiTableFlags_SizingStretchSame))
		{
			for (const int kind : kinds)
			{
				ImGui::TableNextColumn(); ImGui::PushID(kind);
				auto plot = BuildPhononSeriesPlot(group, settings, kind);
				ImGui::TextUnformatted(plot.title.c_str()); ImGui::SameLine();
				PlotExportPopup(plot, project, PhononExportLabel(*m_Session, "series"), m_PlotExports[plot.id]);
				drawFigure(plot, PhononExportLabel(*m_Session, "series"));
				const auto plotId = "##" + plot.id + "/" + settings.plAxis + "/" + settings.plNormalization + "/" + settings.plZpl + "/" + std::to_string(m_Session->PlotRevision());
				if (!plot.series.empty() && ImPlot::BeginPlot(plotId.c_str(), {-1.0f, plotHeight}))
				{
					ImPlot::SetupAxes(" ", " ");
					ImPlot::SetupAxisLimits(ImAxis_X1, plot.x.minimum, plot.x.maximum, ImPlotCond_Once);
					ImPlot::SetupFinish();
					ImRect rect; const auto frame = CurrentPlotFrame(&rect);
					for (const auto &line : plot.series)
					{
						auto initial = line.style; initial.color.reset();
						auto &style = settings.seriesStyles.try_emplace(line.id, initial).first->second;
						if (PlotSeries(line, style, line.id)) m_Session->Changed();
					}
					ImPlot::EndPlot();
					const auto xLabel = plot.x.labelTex + (plot.x.unit.empty() ? "" : " [" + plot.x.unit + "]");
					const auto yLabel = plot.y.labelTex + (plot.y.unit.empty() ? "" : " [" + plot.y.unit + "]");
					DrawTexAxisLabels(frame, rect, xLabel.c_str(), yLabel.c_str());
				}
				if (plot.series.empty()) ImGui::TextDisabled("Brak danych dla tego wykresu.");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}
}
