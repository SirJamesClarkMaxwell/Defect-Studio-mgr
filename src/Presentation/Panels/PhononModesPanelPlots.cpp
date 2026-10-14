#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <tuple>
#include <imgui.h>
#include <implot.h>

#include "Presentation/Panels/PlotWidgets.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

namespace DefectStudio
{
	void PhononModesPanel::drawPlots()
	{
		auto &settings = m_Session->Settings();
		if (!ImGui::BeginTabBar("##phononPlots")) return;
		for (int tab = 0; tab < 3; ++tab)
		{
			const char *title = tab == 0 ? "β(ℏω)" : tab == 1 ? "S_k i S(ℏω)" : "PL";
			if (!ImGui::BeginTabItem(title)) continue;
			const auto active = m_Session->Result();
			std::optional<std::reference_wrapper<const PhononAnalysisResult>> compared;
			if (!active)
				for (const auto &dataset : m_Session->Datasets())
					if (dataset.kind == AnalysisDatasetKind::Phonons && m_Session->IsCompared(dataset.directory))
					{
						std::string error;
						compared = tab == 0 ? m_Session->PlotModes(dataset.directory, error) : m_Session->Spectra(dataset.directory, error);
						if (compared) break;
					}
			if (!active && !compared)
			{
				ImGui::TextDisabled("Wczytaj wynik fononowy lub zaznacz zestaw fononów do porównania.");
				ImGui::EndTabItem();
				continue;
			}
			const auto &result = active ? *active : compared->get();
			ImGui::PushID(tab);
			const std::string plotKey = tab == 0 ? "beta/" : tab == 1 ? "S/" : "PL/";
			if (tab == 2)
			{
				ImGui::TextUnformatted("ZPL"); ImGui::SameLine();
				if (ImGui::BeginCombo("##plZpl", settings.plZpl == "computed" ? "obliczone" :
					settings.plZpl == "spin_purified" ? "po korekcji spinowej" : settings.plZpl == "formula" ? "wzór" : "eksperymentalne"))
				{
					for (const auto &[key, label, available] : {
						std::tuple{"computed", "obliczone", result.zplEv.computed.has_value() && !result.plComputed.empty()},
						std::tuple{"spin_purified", "po korekcji spinowej", result.zplEv.spinPurified.has_value() && !result.plSpinPurified.empty()},
						std::tuple{"experimental", "eksperymentalne", result.zplEv.experimental.has_value() && !result.plExperimental.empty()},
						std::tuple{"formula", "wzór", result.zplEv.formula.has_value() && !result.plFormula.empty()}})
						if (available && ImGui::Selectable(label, settings.plZpl == key)) { m_Session->SetPlZpl(key); }
					ImGui::EndCombo();
				}
			}
			const auto colors = CurrentPlotColors();
			auto plot = tab == 0 ? BuildBetaPlotData(*m_Session, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots, colors) :
				tab == 1 ? BuildSPlotData(*m_Session, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots, colors) :
				BuildPlPlotData(*m_Session, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots, colors);
			if (tab == 2) ImGui::SameLine();
			PlotExportPopup(plot, m_Session->m_ProjectDirectory, PhononExportLabel(*m_Session, result.label), m_PlotExports[plot.id]);
			const bool reset = m_LastPlotRevision != m_Session->PlotRevision() || m_PlotTab != tab;
			if (reset)
			{
				ImPlot::SetNextAxisToFit(ImAxis_Y1);
				if (tab == 1) ImPlot::SetNextAxisToFit(ImAxis_Y2);
			}
			if (ImPlot::BeginPlot("##plot", ImVec2(-1.0f, std::max(ImGui::GetContentRegionAvail().y, 1.0f))))
			{
				ImPlot::SetupAxes(" ", " ");
				if (tab == 1) ImPlot::SetupAxis(ImAxis_Y2, " ", ImPlotAxisFlags_AuxDefault);
				ImPlot::SetupAxisLimits(ImAxis_X1, plot.x.minimum, plot.x.maximum, reset ? ImPlotCond_Always : ImPlotCond_Once);
				ImPlot::SetupFinish();
				ImRect plotRect;
				const ImRect frame = CurrentPlotFrame(&plotRect);
				for (auto &series : plot.series)
				{
					auto &style = settings.seriesStyles[PhononPlotStyleKey(series)];
					const auto legendId = PhononPlotLegendId(series);
					ResolvePlotItemColor(series, style, legendId);
					if (PlotSeries(series, style, legendId)) persist();
					ResolvePlotItemColor(series, style, legendId);
				}
				ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
				if (tab < 2)
				{
					auto modes = active ? m_Session->VisibleModes() : std::vector<std::size_t>{};
					if (!active)
						for (std::size_t i = 0; i < result.modes.size(); ++i)
						{
							const auto &mode = result.modes[i];
							if (!mode.isTranslation && mode.huangRhys >= settings.minHuangRhys &&
								(settings.irrepFilter.empty() || settings.irrepFilter == mode.irrep)) modes.push_back(i);
						}
					auto &otherStyle = settings.seriesStyles[plotKey + "other"];
					auto &a1Style = settings.seriesStyles[plotKey + "A1"];
					if (active && ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						const auto mouse = ImGui::GetIO().MousePos;
						float nearest = 8.0f;
						int selected = -1;
						for (const auto index : modes)
						{
							const auto &mode = result.modes[index];
							if (!(mode.irrep == "A1" ? a1Style.visible : otherStyle.visible)) continue;
							const auto tip = ImPlot::PlotToPixels(mode.frequencyMeV, tab == 0 ? mode.beta : mode.huangRhys);
							const auto base = ImPlot::PlotToPixels(mode.frequencyMeV, 0.0);
							const float distance = std::abs(tip.x - mouse.x);
							if (distance < nearest && mouse.y >= std::min(base.y, tip.y) - 8.0f && mouse.y <= std::max(base.y, tip.y) + 8.0f)
							{
								nearest = distance;
								selected = static_cast<int>(index);
							}
						}
						if (selected >= 0) selectMode(selected);
					}
				}
				ImPlot::EndPlot();
				m_LastPlotRevision = m_Session->PlotRevision();
				m_PlotTab = tab;
				DrawTexAxisLabels(frame, plotRect, tab == 2 ? "E [eV], E_{ZPL}" : "\\hbar\\omega [meV]",
					tab == 0 ? "\\beta = N/N_{eff}" : tab == 1 ? "S_k" : "PL [j.u.]");
				if (tab == 1)
					DrawTexText(ImGui::GetWindowDrawList(), ImVec2(frame.Max.x - ImPlot::GetStyle().PlotPadding.x - ImGui::GetFontSize(),
						0.5f * (plotRect.Min.y + plotRect.Max.y + TexTextSize(ParseTexLite("S(\\hbar\\omega) [meV^{-1}]"), ImGui::GetFontSize()).x)),
						ImGui::GetColorU32(ImGuiCol_Text), "S(\\hbar\\omega) [meV^{-1}]", true);
			}
			if (tab == 2 && result.plComputed.empty() && result.plSpinPurified.empty() && result.plExperimental.empty() && result.plFormula.empty())
				ImGui::TextDisabled("Brak widma PL w wyniku.");
			ImGui::PopID();
			ImGui::EndTabItem();
		}
		drawCcdPlotTabs();
		if (ImGui::BeginTabItem("Dyspersja"))
		{
			drawDispersionPlot();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}
