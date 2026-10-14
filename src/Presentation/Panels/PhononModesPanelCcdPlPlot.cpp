#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <span>
#include <imgui.h>
#include <implot.h>
#include "Presentation/Panels/PlotWidgets.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

namespace DefectStudio
{
	void PhononModesPanel::drawCcdPlPlot(const CcdAnalysisResult &result)
	{
		auto &s = m_Session->Settings();
		ImGui::Text("ZPL %.5g eV (%s)", result.zplEv, result.zplFormulaText.empty() ? "z wyniku" : result.zplFormulaText.c_str());
		if (ImGui::Checkbox("porównaj z PL fononów", &s.ccdComparePhononPl)) { m_Session->InvalidatePlots(); persist(); }
		if (s.ccdComparePhononPl)
		{
			if (!m_Session->Result()) ImGui::TextDisabled("Wczytaj aktywny zestaw fononów do porównania.");
			else ImGui::TextDisabled("PL fononów: ZPL %s", PhononPanelSession::PlCurveChoice(*m_Session->Result(), s.plZpl).c_str());
		}
		auto plot = BuildCcdPlPlotData(*m_Session, result, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots, CurrentPlotColors());
		PlotExportPopup(plot, m_Session->m_ProjectDirectory, PhononExportLabel(*m_Session, result.label), m_PlotExports[plot.id]);
		const double minimum = plot.x.minimum, maximum = plot.x.maximum;
		std::vector<std::string> errors;
		for (const auto &dataset : m_Session->Datasets())
			if (dataset.kind == AnalysisDatasetKind::Ccd && m_Session->IsCompared(dataset.directory) &&
				!PhononPanelSession::SameFilePath(dataset.directory, s.selectedCcdDirectory, m_Session->m_ProjectDirectory))
			{
				std::string error;
				if (!m_Session->Ccd(dataset.directory, error)) errors.push_back(dataset.label + ": " + error);
			}
		const bool reset = m_LastPlotRevision != m_Session->PlotRevision() || m_PlotTab != 4;
		if (reset) ImPlot::SetNextAxisToFit(ImAxis_Y1);
		if (!ImPlot::BeginPlot("##ccdPlPlot", ImVec2(-1, std::max(ImGui::GetContentRegionAvail().y, 1.0f)))) return;
		ImPlot::SetupAxes(" ", " ");
		ImPlot::SetupAxisLimits(ImAxis_X1, minimum, maximum, reset ? ImPlotCond_Always : ImPlotCond_Once);
		ImPlot::SetupFinish();
		ImRect rect; const auto frame = CurrentPlotFrame(&rect);
		for (auto &series : plot.series)
		{
			auto &style = s.seriesStyles[PhononPlotStyleKey(series)];
			const auto legendId = PhononPlotLegendId(series);
			ResolvePlotItemColor(series, style, legendId);
			if (PlotSeries(series, style, legendId)) persist();
			ResolvePlotItemColor(series, style, legendId);
		}
		ImPlot::EndPlot();
		DrawTexAxisLabels(frame, rect, "E [eV]", "PL 1D [j.u.]");
		m_LastPlotRevision = m_Session->PlotRevision(); m_PlotTab = 4;
		if (result.pl1d.numerov.empty() && result.pl1d.harmonic.empty() && result.pl1d.phonon1d.empty())
			ImGui::TextDisabled("Brak widma pl_1d.tsv w tym zestawie.");
		for (const auto &error : errors) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", error.c_str());
	}
}
