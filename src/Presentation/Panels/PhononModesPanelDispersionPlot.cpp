#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <imgui.h>
#include <implot.h>
#include "Presentation/Panels/PlotWidgets.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

namespace DefectStudio
{
	void PhononModesPanel::drawDispersionPlot()
	{
		if (!m_Session->m_Dispersion)
		{
			ImGui::TextDisabled("Wczytaj lub policz dyspersję w oknie Fonony.");
			return;
		}
		auto &settings = m_Session->Settings();
		const auto selectedLabel = PhononPanelSession::DirectoryLabel(settings.selectedDispersionDirectory, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots);
		ImGui::TextUnformatted(selectedLabel.c_str());
		ImGui::SetItemTooltip("%s", settings.selectedDispersionDirectory.Utf8().c_str());
		const auto &result = *m_Session->m_Dispersion;
		ImGui::Text("pasm: %d, max ℏω = %.2f meV", result.bandCount, result.maxMeV);
		if (result.qpoints.empty()) ImGui::TextDisabled("Przelicz dyspersję, aby wybierać q na wykresie.");
		auto plot = BuildDispersionPlotData(result, settings, settings.selectedDispersionDirectory, CurrentPlotColors());
		PlotExportPopup(plot, m_Session->m_ProjectDirectory, PhononExportLabel(*m_Session, selectedLabel), m_PlotExports[plot.id]);
		drawFigure(plot, PhononExportLabel(*m_Session, selectedLabel));
		const auto inputPlot = ImGui::GetID("##bulkDispersion");
		if (!ImPlot::BeginPlot("##bulkDispersion", ImVec2(-1.0f, std::max(ImGui::GetContentRegionAvail().y, 1.0f)))) return;
		ImPlot::SetupAxes(nullptr, "ℏω [meV]", ImPlotAxisFlags_NoGridLines, 0);
		const auto condition = (m_LastPlotRevision != m_Session->PlotRevision() || m_PlotTab != 5) ? ImPlotCond_Always : ImPlotCond_Once;
		ImPlot::SetupAxisLimits(ImAxis_X1, result.distance.front(), std::max(result.distance.back(), result.distance.front() + 1e-6), condition);
		ImPlot::SetupAxisLimits(ImAxis_Y1, std::min(0.0, result.minMeV) - 5.0, std::max(0.0, result.maxMeV) + 5.0, condition);
		std::vector<double> ticks;
		std::vector<const char *> labels;
		for (const auto &tick : result.ticks) { ticks.push_back(tick.distance); labels.push_back(tick.label.c_str()); }
		ImPlot::SetupAxisTicks(ImAxis_X1, ticks.data(), static_cast<int>(ticks.size()), labels.data(), false);
		for (auto &series : plot.series)
		{
			auto &style = settings.seriesStyles[PhononPlotStyleKey(series)];
			ResolvePlotItemColor(series, style);
			if (PlotSeries(series, style)) persist();
			ResolvePlotItemColor(series, style);
		}
		ImPlotSpec guide;
		guide.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
		guide.LineColor = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
		ImPlot::PlotInfLines("##ticks", ticks.data(), static_cast<int>(ticks.size()), guide);
		guide.LineColor = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
		guide.LineWeight = 2.0f;
		if (!result.breaks.empty()) ImPlot::PlotInfLines("##breaks", result.breaks.data(), static_cast<int>(result.breaks.size()), guide);
		guide.Flags |= ImPlotInfLinesFlags_Horizontal;
		const double zero = 0.0;
		ImPlot::PlotInfLines("##zero", &zero, 1, guide);
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			m_Session->m_DispersionClickArmed = m_Session->m_DispersionDragHeld = false;
			m_Session->m_DispersionInputPlot = 0;
		}
		auto row = m_Session->DispersionRow();
		bool clicked = false, hovered = false, held = false;
		const bool plotHovered = ImPlot::IsPlotHovered() && !ImPlot::GetCurrentPlot()->Items.Legend.Hovered;
		const bool ownsInput = m_Session->m_DispersionInputPlot == 0 || m_Session->m_DispersionInputPlot == inputPlot;
		if (row && settings.bulkBand >= 0 && static_cast<std::size_t>(settings.bulkBand) < result.bandsMeV.size())
		{
			double distance = result.distance[*row];
			const ImVec4 accent(1.0f, 0.8f, 0.2f, 1.0f);
			ImPlot::DragLineX(108, &distance, accent, 1.5f,
				ImPlotDragToolFlags_NoFit | ImPlotDragToolFlags_Delayed | (ownsInput ? 0 : ImPlotDragToolFlags_NoInputs),
				&clicked, &hovered, &held);
			if (ownsInput)
			{
				const bool wasHeld = m_Session->m_DispersionDragHeld;
				if (held || wasHeld)
				{
					if (!held && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) distance = ImPlot::GetPlotMousePos().x;
					row = SnapToSegment(result, m_Session->m_PickedDispersionSegment, distance);
					m_Session->m_PickedDispersionRow = row;
					m_Session->m_DispersionInputPlot = inputPlot;
					m_Session->m_DispersionClickArmed = false;
				}
				m_Session->m_DispersionDragHeld = held;
				if (wasHeld && !held && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
				{
					m_Session->PickDispersionRow(*row, settings.bulkBand);
					m_Session->m_DispersionInputPlot = 0;
				}
			}
			if (*row < result.bandsMeV[settings.bulkBand].size())
			{
				const double x = result.distance[*row], frequency = result.bandsMeV[settings.bulkBand][*row];
				ImPlotSpec marker;
				marker.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
				marker.Marker = ImPlotMarker_Circle;
				marker.MarkerSize = 6.0f;
				marker.MarkerFillColor = marker.MarkerLineColor = accent;
				ImPlot::PlotScatter("##pickedMode", &x, &frequency, 1, marker);
				if (held || hovered)
				{
					const auto &q = result.qpoints[*row];
					ImGui::SetTooltip("q = (%.6g, %.6g, %.6g), ℏω = %.3f meV", q.x, q.y, q.z, frequency);
				}
			}
		}
		if (ownsInput && !result.qpoints.empty())
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && plotHovered &&
				!clicked && !held && !hovered)
			{
				m_Session->m_DispersionClickArmed = true;
				m_Session->m_DispersionInputPlot = inputPlot;
			}
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			{
				if (m_Session->m_DispersionClickArmed && plotHovered &&
					ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] <= 16.0f)
				{
					const auto mouse = ImPlot::GetPlotMousePos();
					const auto limits = ImPlot::GetPlotLimits();
					const auto size = ImPlot::GetPlotSize();
					if (const auto pick = PickDispersion(result, mouse.x, mouse.y,
						std::abs(limits.X.Max - limits.X.Min) / size.x, std::abs(limits.Y.Max - limits.Y.Min) / size.y))
						m_Session->PickDispersionRow(pick->row, pick->band);
				}
				m_Session->m_DispersionClickArmed = false;
				m_Session->m_DispersionInputPlot = 0;
			}
		}
		ImPlot::EndPlot();
		m_LastPlotRevision = m_Session->PlotRevision();
		m_PlotTab = 5;
	}
}
