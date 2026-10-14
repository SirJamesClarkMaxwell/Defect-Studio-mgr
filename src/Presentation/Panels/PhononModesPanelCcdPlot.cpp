#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <imgui.h>
#include <implot.h>
#include "Presentation/Panels/PlotWidgets.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

namespace DefectStudio
{
	namespace
	{
		std::optional<double> Potential(const CcdAnalysisResult &result, bool excited, double q)
		{
			const auto &grid = result.curves.q;
			const auto &values = excited ? result.curves.vEsEv : result.curves.vGsEv;
			if (grid.empty() || values.size() != grid.size() || q < grid.front() || q > grid.back()) return std::nullopt;
			const auto it = std::lower_bound(grid.begin(), grid.end(), q);
			const auto i = static_cast<std::size_t>(it - grid.begin());
			if (i == 0 || *it == q) return values[i];
			return std::lerp(values[i - 1], values[i], (q - grid[i - 1]) / (grid[i] - grid[i - 1]));
		}

		PlotSeriesStyle BranchStyle(const PlotSeriesStyle &style, ImU32 color)
		{
			auto branch = style;
			const auto c = ImGui::ColorConvertU32ToFloat4(color);
			if (!branch.color) branch.color = glm::vec4(c.x, c.y, c.z, c.w);
			return branch;
		}

		void ArrowHead(ImDrawList &draw, const ImVec2 &from, const ImVec2 &tip, ImU32 color, float weight)
		{
			const float dx = tip.x - from.x, dy = tip.y - from.y;
			const float length = std::hypot(dx, dy);
			if (length < 1) return;
			const float x = dx / length, y = dy / length, size = 5.0f + weight;
			draw.AddTriangleFilled(tip, ImVec2(tip.x - size * x + size * 0.5f * y, tip.y - size * y - size * 0.5f * x),
				ImVec2(tip.x - size * x - size * 0.5f * y, tip.y - size * y + size * 0.5f * x), color);
		}

		void DrawTransitions(const CcdAnalysisResult &result, const PlotSeriesStyle &style, double minimum, double maximum)
		{
			PlotStyledLine("Strzałki", {}, {}, style, ImPlotItemFlags_NoFit);
			if (!ImPlot::GetItem("Strzałki")->Show) return;
			const auto gs0 = Potential(result, false, 0), es0 = Potential(result, true, 0);
			const auto gs1 = Potential(result, false, result.deltaQ), es1 = Potential(result, true, result.deltaQ);
			if (!gs0 || !es0 || !gs1 || !es1) return;
			const ImU32 color = ImPlot::GetItem("Strzałki")->Color;
			const auto annotationColor = ImGui::ColorConvertU32ToFloat4(color);
			auto line = style;
			line.filled = false;
			line.color = glm::vec4(annotationColor.x, annotationColor.y, annotationColor.z, annotationColor.w);
			auto &draw = *ImPlot::GetPlotDrawList();
			const auto segment = [&](double x0, double y0, double x1, double y1) {
				PlotStyledLine("Strzałki", std::array{x0, x1}, std::array{y0, y1}, line, ImPlotItemFlags_NoFit);
			};
			const auto arrow = [&](double q, double from, double to) {
				segment(q, from, q, to);
				ImPlot::PushPlotClipRect();
				ArrowHead(draw, ImPlot::PlotToPixels(q, from), ImPlot::PlotToPixels(q, to), color, style.lineWeight);
				ImPlot::PopPlotClipRect();
			};
			arrow(0, *gs0, *es0);
			arrow(result.deltaQ, *es1, *gs1);
			ImPlot::Annotation(0, 0.5 * (*gs0 + *es0), annotationColor, ImVec2(-12, 0), true,
				"absorpcja %.4g eV", result.franckCondon.absorptionEv.value_or(*es0 - *gs0));
			ImPlot::Annotation(result.deltaQ, 0.5 * (*es1 + *gs1), annotationColor, ImVec2(12, 0), true,
				"emisja %.4g eV", result.franckCondon.emissionEv.value_or(*es1 - *gs1));
			auto dashed = line; dashed.lineStyle = 1;
			PlotStyledLine("Strzałki", std::array{minimum, result.deltaQ}, std::array{result.zplEv, result.zplEv}, dashed, ImPlotItemFlags_NoFit);
			ImPlot::Annotation(minimum, result.zplEv, annotationColor, ImVec2(6, -12), true, "ZPL %.4g eV", result.zplEv);
			const auto bracket = [&](double q, double bottom, double top, const char *label, double energy) {
				segment(q, bottom, q, top);
				ImPlot::PushPlotClipRect();
				for (double e : {bottom, top})
				{
					const auto p = ImPlot::PlotToPixels(q, e);
					draw.AddLine(ImVec2(p.x - 4, p.y), ImVec2(p.x + 4, p.y), color, style.lineWeight);
				}
				ImPlot::PopPlotClipRect();
				ImPlot::Annotation(q, 0.5 * (bottom + top), annotationColor, ImVec2(6, 0), true, "%s %.4g eV", label, energy);
			};
			const double offset = 0.12 * (maximum - minimum);
			bracket(result.deltaQ + offset, *gs0, *gs1, "E_rel(gs)", result.franckCondon.relaxationGsEv.value_or(*gs1 - *gs0));
			bracket(-offset, *es1, *es0, "E_rel(es)", result.franckCondon.relaxationEsEv.value_or(*es0 - *es1));
			const double below = -0.07 * std::max(result.zplEv, 1.0);
			segment(0, below, result.deltaQ, below);
			ImPlot::PushPlotClipRect();
			const auto a = ImPlot::PlotToPixels(0, below), b = ImPlot::PlotToPixels(result.deltaQ, below);
			ArrowHead(draw, a, b, color, style.lineWeight); ArrowHead(draw, b, a, color, style.lineWeight);
			ImPlot::PopPlotClipRect();
			ImPlot::Annotation(0.5 * result.deltaQ, below, annotationColor, ImVec2(0, 12), true,
				"ΔQ = %.4g amu^½ Å", result.deltaQ);
		}
	}

	void PhononModesPanel::drawCcdPlotTabs()
	{
		for (int tab = 3; tab < 5; ++tab)
		{
			if (!ImGui::BeginTabItem(tab == 3 ? "CCD" : "PL 1D")) continue;
			ImGui::PushID(tab);
			if (const auto result = m_Session->CcdResult())
			{
				const auto &s = m_Session->Settings();
				const auto label = PhononPanelSession::DirectoryLabel(s.selectedCcdDirectory, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots);
				ImGui::TextUnformatted(label.c_str()); ImGui::SetItemTooltip("%s", s.selectedCcdDirectory.Utf8().c_str());
				if (tab == 3) drawCcdPlot(result->get());
				else drawCcdPlPlot(result->get());
			}
			else ImGui::TextDisabled("Wybierz zestaw CCD w Zestawach lub wczytaj / policz CCD w sekcji CCD / PL 1D.");
			ImGui::PopID();
			ImGui::EndTabItem();
		}
	}

	void PhononModesPanel::drawCcdPlot(const CcdAnalysisResult &result)
	{
		auto &s = m_Session->Settings();
		const auto number = [](const std::optional<double> &v) { return v ? std::format("{:.4g}", *v) : "-"; };
		ImGui::Text("ZPL %.5g eV (%s)", result.zplEv, result.zplFormulaText.empty() ? "z wyniku" : result.zplFormulaText.c_str());
		ImGui::Text("ℏω gs/es: %s / %s meV; S gs/es: %s / %s",
			number(result.harmonic.gs ? std::optional(result.harmonic.gs->hbarOmegaMeV) : std::nullopt).c_str(),
			number(result.harmonic.es ? std::optional(result.harmonic.es->hbarOmegaMeV) : std::nullopt).c_str(),
			number(result.harmonic.gs ? std::optional(result.harmonic.gs->huangRhys) : std::nullopt).c_str(),
			number(result.harmonic.es ? std::optional(result.harmonic.es->huangRhys) : std::nullopt).c_str());
		ImGui::Text("absorpcja %s eV; emisja %s eV; źródło: %s", number(result.franckCondon.absorptionEv).c_str(),
			number(result.franckCondon.emissionEv).c_str(), result.franckCondon.source.empty() ? "-" : result.franckCondon.source.c_str());
		ImGui::TextUnformatted("poziomy"); ImGui::SameLine(); ImGui::SetNextItemWidth(65);
		if (ImGui::DragInt("##levels", &s.ccdLevels, 0.1f, 0, 8, "%d", ImGuiSliderFlags_AlwaysClamp)) persist();
		ImGui::SameLine(); if (ImGui::Checkbox("funkcje falowe", &s.ccdWavefunctions)) persist();
		ImGui::SameLine(); if (ImGui::Checkbox("strzałki", &s.ccdArrows)) persist();
		if (s.ccdWavefunctions)
		{
			if (ImGui::RadioButton("χ", !s.ccdProbability)) { s.ccdProbability = false; persist(); }
			ImGui::SameLine(); if (ImGui::RadioButton("|χ|²", s.ccdProbability)) { s.ccdProbability = true; persist(); }
			ImGui::SameLine(); ImGui::TextUnformatted("skala χ"); ImGui::SameLine(); ImGui::SetNextItemWidth(90);
			float scale = static_cast<float>(s.ccdWavefunctionScale);
			if (ImGui::DragFloat("##waveScale", &scale, 0.001f, 0, 1, "%.3g", ImGuiSliderFlags_AlwaysClamp))
			{ s.ccdWavefunctionScale = scale; persist(); }
			if (result.wavefunctions.q.empty()) ImGui::TextDisabled("Brak wavefunctions.tsv w tym zestawie.");
		}
		auto plot = BuildCcdPlotData(result, s, s.selectedCcdDirectory, CurrentPlotColors());
		PlotExportPopup(plot, m_Session->m_ProjectDirectory, PhononExportLabel(*m_Session, result.label), m_PlotExports[plot.id]);
		const double minimum = plot.x.minimum, maximum = plot.x.maximum;
		const bool reset = m_LastPlotRevision != m_Session->PlotRevision() || m_PlotTab != 3;
		if (reset) ImPlot::SetNextAxisToFit(ImAxis_Y1);
		if (!ImPlot::BeginPlot("##ccdPlot", ImVec2(-1, std::max(ImGui::GetContentRegionAvail().y, 1.0f)))) return;
		ImPlot::SetupAxes(" ", " ");
		ImPlot::SetupAxisLimits(ImAxis_X1, minimum, maximum, reset ? ImPlotCond_Always : ImPlotCond_Once);
		ImPlot::SetupFinish();
		ImRect rect; const auto frame = CurrentPlotFrame(&rect);
		for (auto &series : plot.series)
			if (series.id == "CCD/gs" || series.id == "CCD/es")
			{
				auto &style = s.seriesStyles[series.id];
				ResolvePlotItemColor(series, style);
				if (PlotSeries(series, style)) persist();
				ResolvePlotItemColor(series, style);
			}
		const ImU32 gsColor = ImPlot::GetItem("V_gs")->Color, esColor = ImPlot::GetItem("V_es")->Color;
		auto &pointsStyle = s.seriesStyles["CCD/points"];
		PlotStyledLine("Punkty", {}, {}, pointsStyle);
		for (auto &series : plot.series)
			if (series.kind == PlotSeriesKind::Scatter)
			{
				if (!pointsStyle.color && (series.id.starts_with("CCD/points/gs/") || series.id.starts_with("CCD/points/es/")))
					series.style = BranchStyle(pointsStyle, series.id.starts_with("CCD/points/gs/") ? gsColor : esColor);
				if (PlotSeries(series, pointsStyle)) persist();
			}
		for (const auto &point : result.points)
		{
			const double energy = CcdPlotPointEnergy(result, point);
			const auto p = ImPlot::PlotToPixels(point.q, energy), mouse = ImGui::GetIO().MousePos;
			if (pointsStyle.visible && ImPlot::IsPlotHovered() && std::hypot(p.x - mouse.x, p.y - mouse.y) < 8)
				ImGui::SetTooltip("%s, t=%.4g, Q=%.4g\nE=%s eV\n%s%s", point.branch.c_str(), point.t, point.q,
					number(point.energyEv).c_str(), point.status.c_str(), point.energyEv ? "" : " (marker orientacyjny; brak energii)");
		}
		if (SeriesLegendPopup("Punkty", pointsStyle, false)) persist();
		auto &levelStyle = s.seriesStyles["CCD/levels"];
		PlotStyledLine("Poziomy", {}, {}, levelStyle);
		for (bool excited : {false, true})
		{
			const auto &levels = excited ? result.levels.esMeV : result.levels.gsMeV;
			const auto color = excited ? esColor : gsColor;
			auto &waveStyle = s.seriesStyles[excited ? "CCD/chi_es" : "CCD/chi_gs"];
			const char *label = excited ? "χ es" : "χ gs";
			if (s.ccdWavefunctions) PlotStyledLine(label, {}, {}, BranchStyle(waveStyle, color));
			for (auto &series : plot.series)
				if (series.id.starts_with(excited ? "CCD/levels/es/" : "CCD/levels/gs/") ||
					series.id.starts_with(excited ? "CCD/chi_es/" : "CCD/chi_gs/"))
				{
					auto &style = s.seriesStyles[PhononPlotStyleKey(series)];
					series.style = BranchStyle(style, color);
					if (PlotSeries(series, style, false)) persist();
				}
			for (std::size_t n = 0; n < std::min(levels.size(), static_cast<std::size_t>(s.ccdLevels)); ++n)
			{
				const double energy = (excited ? result.zplEv : 0) + levels[n] / 1000;
				if (const auto range = PhononPanelSession::CcdLevelRange(result, excited, n))
				{
					const auto mouse = ImPlot::GetPlotMousePos();
					const auto pixel = ImPlot::PlotToPixels(mouse.x, energy);
					if (levelStyle.visible && ImPlot::IsPlotHovered() && mouse.x >= range->first && mouse.x <= range->second &&
						std::abs(pixel.y - ImGui::GetIO().MousePos.y) < 5)
						ImGui::SetTooltip("%s n=%zu: %.4g meV ponad minimum\nFC(0,n) = %s", excited ? "es" : "gs", n, levels[n],
							(!excited && n < result.fc0n.size() ? std::format("{:.5g}", result.fc0n[n]) : "-").c_str());
				}
			}
			if (s.ccdWavefunctions && SeriesLegendPopup(label, waveStyle, false)) persist();
		}
		if (SeriesLegendPopup("Poziomy", levelStyle, false)) persist();
		if (s.ccdArrows)
		{
			auto &style = s.seriesStyles["CCD/arrows"];
			DrawTransitions(result, style, minimum, maximum);
			if (SeriesLegendPopup("Strzałki", style, false)) persist();
		}
		// Fit includes space below the wells for the Delta Q annotation, even though arrows do not affect fit.
		ImPlot::FitPoint(ImPlotPoint(minimum, -0.16 * std::max(result.zplEv, 1.0)));
		ImPlot::EndPlot();
		DrawTexAxisLabels(frame, rect, "Q [amu^{1/2} Å]", "E [eV]");
		m_LastPlotRevision = m_Session->PlotRevision(); m_PlotTab = 3;
		if (result.curves.q.empty()) ImGui::TextDisabled("Brak ccd_curves.tsv w tym zestawie.");
	}
}
