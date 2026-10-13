#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <tuple>
#include <imgui.h>
#include <implot.h>

#include "Presentation/Panels/PlotWidgets.hpp"

namespace DefectStudio
{
	void PhononModesPanel::drawPlots()
	{
		const auto &result = *m_Session->Result();
		auto &settings = m_Session->Settings();
		const float height = ImGui::GetTextLineHeightWithSpacing() * 18.0f;
		if (!ImGui::BeginTabBar("##phononPlots")) return;
		for (int tab = 0; tab < 3; ++tab)
		{
			const char *title = tab == 0 ? "β(ℏω)" : tab == 1 ? "S_k i S(ℏω)" : "PL";
			if (!ImGui::BeginTabItem(title)) continue;
			ImGui::PushID(tab);
			const std::string plotKey = tab == 0 ? "beta/" : tab == 1 ? "S/" : "PL/";
			// Keep each tab aligned with the filter row above the mode table.
			if (tab < 2) ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
			else if (ImGui::BeginCombo("ZPL", settings.plZpl == "computed" ? "obliczone" :
				settings.plZpl == "spin_purified" ? "po korekcji spinowej" : "eksperymentalne"))
			{
				for (const auto &[key, label, available] : {
					std::tuple{"computed", "obliczone", result.zplEv.computed.has_value() && !result.plComputed.empty()},
					std::tuple{"spin_purified", "po korekcji spinowej", result.zplEv.spinPurified.has_value() && !result.plSpinPurified.empty()},
					std::tuple{"experimental", "eksperymentalne", result.zplEv.experimental.has_value() && !result.plExperimental.empty()}})
					if (available && ImGui::Selectable(label, settings.plZpl == key)) { settings.plZpl = key; m_ResetPlots = true; persist(); }
				ImGui::EndCombo();
			}
			const bool reset = m_ResetPlots || m_PlotTab != tab;
			if (reset)
			{
				ImPlot::SetNextAxisToFit(ImAxis_Y1);
				if (tab == 1) ImPlot::SetNextAxisToFit(ImAxis_Y2);
			}
			if (ImPlot::BeginPlot("##plot", ImVec2(-1.0f, height)))
			{
				ImPlot::SetupAxes(" ", " ");
				if (tab == 1) ImPlot::SetupAxis(ImAxis_Y2, " ", ImPlotAxisFlags_AuxDefault);
				const auto [minimum, maximum] = tab < 2 ? m_Session->ModeEnergyRangeMeV() : m_Session->PlEnergyRangeEv();
				ImPlot::SetupAxisLimits(ImAxis_X1, minimum, maximum, reset ? ImPlotCond_Always : ImPlotCond_Once);
				ImPlot::SetupFinish();
				ImRect plotRect;
				const ImRect frame = CurrentPlotFrame(&plotRect);
				if (tab < 2)
				{
					const auto modes = m_Session->VisibleModes();
					std::vector<double> a1x, a1y, otherx, othery;
					for (const auto index : modes)
					{
						const auto &mode = result.modes[index];
						const bool a1 = mode.irrep == "A1";
						(a1 ? a1x : otherx).push_back(mode.frequencyMeV);
						(a1 ? a1y : othery).push_back(tab == 0 ? mode.beta : mode.huangRhys);
					}
					auto &otherStyle = settings.seriesStyles[plotKey + "other"];
					auto &a1Style = settings.seriesStyles[plotKey + "A1"];
					PlotStyledStems("inne irreps", otherx, othery, otherStyle);
					if (SeriesLegendPopup("inne irreps", otherStyle, false)) persist();
					PlotStyledStems("A1", a1x, a1y, a1Style);
					if (SeriesLegendPopup("A1", a1Style, false)) persist();
					if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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
					if (const auto selected = m_Session->SelectedMode())
					{
						auto &style = settings.seriesStyles[plotKey + "selected"];
						PlotStyledInfLine("Wybrany mod", result.modes[*selected].frequencyMeV, style);
						if (SeriesLegendPopup("Wybrany mod", style, false)) persist();
					}
					if (tab == 1 && !result.spectralEnergyMeV.empty())
					{
						ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
						auto &style = settings.seriesStyles[plotKey + "density"];
						const auto [first, last] = PhononPanelSession::GridIndexRange(result.spectralEnergyMeV, minimum, maximum);
						PlotStyledLine("S(ℏω)", std::span(result.spectralEnergyMeV).subspan(first, last - first),
							std::span(result.spectralDensityPerMeV).subspan(first, last - first), style);
						if (SeriesLegendPopup("S(ℏω)", style)) persist();
					}
				}
				else
				{
					const auto &curve = settings.plZpl == "computed" ? result.plComputed :
						settings.plZpl == "spin_purified" ? result.plSpinPurified : result.plExperimental;
					if (!curve.empty())
					{
						auto &style = settings.seriesStyles[plotKey + "PL"];
						const auto [first, last] = PhononPanelSession::GridIndexRange(result.plPhotonEnergyEv, minimum, maximum);
						PlotStyledLine("PL", std::span(result.plPhotonEnergyEv).subspan(first, last - first),
							std::span(curve).subspan(first, last - first), style);
						if (SeriesLegendPopup("PL", style)) persist();
					}
				}
				ImPlot::EndPlot();
				m_ResetPlots = false;
				m_PlotTab = tab;
				DrawTexAxisLabels(frame, plotRect, tab == 2 ? "E [eV], E_{ZPL}" : "\\hbar\\omega [meV]",
					tab == 0 ? "\\beta = N/N_{eff}" : tab == 1 ? "S_k" : "PL [j.u.]");
				if (tab == 1)
					DrawTexText(ImGui::GetWindowDrawList(), ImVec2(frame.Max.x - ImPlot::GetStyle().PlotPadding.x - ImGui::GetFontSize(),
						0.5f * (plotRect.Min.y + plotRect.Max.y + TexTextSize(ParseTexLite("S(\\hbar\\omega) [meV^{-1}]"), ImGui::GetFontSize()).x)),
						ImGui::GetColorU32(ImGuiCol_Text), "S(\\hbar\\omega) [meV^{-1}]", true);
			}
			if (tab == 2 && result.plComputed.empty() && result.plSpinPurified.empty() && result.plExperimental.empty())
				ImGui::TextDisabled("Brak widma PL w wyniku.");
			ImGui::PopID();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}
