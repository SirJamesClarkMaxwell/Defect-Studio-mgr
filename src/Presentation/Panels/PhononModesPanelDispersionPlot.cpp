#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <span>
#include <imgui.h>
#include <implot.h>
#include "Presentation/Panels/PlotWidgets.hpp"

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
		if (!ImPlot::BeginPlot("##bulkDispersion", ImVec2(-1.0f, std::max(ImGui::GetContentRegionAvail().y, 1.0f)))) return;
		ImPlot::SetupAxes(nullptr, "ℏω [meV]", ImPlotAxisFlags_NoGridLines, 0);
		const auto condition = (m_LastPlotRevision != m_Session->PlotRevision() || m_PlotTab != 5) ? ImPlotCond_Always : ImPlotCond_Once;
		ImPlot::SetupAxisLimits(ImAxis_X1, result.distance.front(), std::max(result.distance.back(), result.distance.front() + 1e-6), condition);
		ImPlot::SetupAxisLimits(ImAxis_Y1, std::min(0.0, result.minMeV) - 5.0, std::max(0.0, result.maxMeV) + 5.0, condition);
		std::vector<double> ticks;
		std::vector<const char *> labels;
		for (const auto &tick : result.ticks) { ticks.push_back(tick.distance); labels.push_back(tick.label.c_str()); }
		ImPlot::SetupAxisTicks(ImAxis_X1, ticks.data(), static_cast<int>(ticks.size()), labels.data(), false);
		auto &bandStyle = settings.seriesStyles["dispersion/bands"];
		for (std::size_t start = 0; start < result.segment.size();)
		{
			std::size_t end = start + 1;
			while (end < result.segment.size() && result.segment[end] == result.segment[start]) ++end;
			for (int band = 0; band < result.bandCount; ++band)
				PlotStyledLine("Pasma", std::span<const double>(result.distance).subspan(start, end - start),
					std::span<const double>(result.bandsMeV[band]).subspan(start, end - start), bandStyle);
			start = end;
		}
		if (SeriesLegendPopup("Pasma", bandStyle)) persist();
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
		ImPlot::EndPlot();
		m_LastPlotRevision = m_Session->PlotRevision();
		m_PlotTab = 5;
	}
}
