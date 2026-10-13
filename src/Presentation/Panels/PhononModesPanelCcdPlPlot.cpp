#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <span>
#include <imgui.h>
#include <implot.h>
#include "Presentation/Panels/PlotWidgets.hpp"

namespace DefectStudio
{
	void PhononModesPanel::drawCcdPlPlot(const CcdAnalysisResult &result)
	{
		auto &s = m_Session->Settings();
		ImGui::Text("ZPL %.5g eV (%s)", result.zplEv, result.zplFormulaText.empty() ? "z wyniku" : result.zplFormulaText.c_str());
		if (ImGui::Checkbox("porównaj z PL fononów", &s.ccdComparePhononPl)) { m_ResetPlots = true; persist(); }
		if (s.ccdComparePhononPl)
		{
			if (!m_Session->Result()) ImGui::TextDisabled("Wczytaj aktywny zestaw fononów do porównania.");
			else ImGui::TextDisabled("PL fononów: ZPL %s", PhononPanelSession::PlCurveChoice(*m_Session->Result(), s.plZpl).c_str());
		}
		auto [minimum, maximum] = PhononPanelSession::UsefulRange(result);
		std::vector<std::string> errors;
		for (const auto &dataset : m_Session->Datasets())
			if (dataset.kind == AnalysisDatasetKind::Ccd && m_Session->IsCompared(dataset.directory) &&
				!PhononPanelSession::SameFilePath(dataset.directory, s.selectedCcdDirectory, m_ProjectDirectory))
			{
				std::string error;
				if (const auto ccd = m_Session->Ccd(dataset.directory, error))
				{
					const auto [low, high] = PhononPanelSession::UsefulRange(ccd->get());
					minimum = std::min(minimum, low); maximum = std::max(maximum, high);
				}
				else errors.push_back(dataset.label + ": " + error);
			}
		if (s.ccdComparePhononPl && m_Session->Result())
		{
			const auto [low, high] = PhononPanelSession::UsefulRange(*m_Session->Result(), true, s.plZpl);
			minimum = std::min(minimum, low); maximum = std::max(maximum, high);
		}
		const bool reset = m_ResetPlots || m_PlotTab != 4;
		if (reset) ImPlot::SetNextAxisToFit(ImAxis_Y1);
		if (!ImPlot::BeginPlot("##ccdPlPlot", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 20))) return;
		ImPlot::SetupAxes(" ", " ");
		ImPlot::SetupAxisLimits(ImAxis_X1, minimum, maximum, reset ? ImPlotCond_Always : ImPlotCond_Once);
		ImPlot::SetupFinish();
		ImRect rect; const auto frame = CurrentPlotFrame(&rect);
		const auto draw = [&](const std::string &label, const std::string &key, const std::vector<double> &x, const std::vector<double> &y) {
			if (x.empty() || y.size() != x.size()) return;
			const auto [first, last] = PhononPanelSession::GridIndexRange(x, minimum, maximum);
			auto &style = s.seriesStyles[key];
			PlotStyledLine(label.c_str(), std::span(x).subspan(first, last - first), std::span(y).subspan(first, last - first), style);
			if (SeriesLegendPopup(label.c_str(), style)) persist();
		};
		draw("Numerov", "PL1D/numerov", result.pl1d.energyEv, result.pl1d.numerov);
		draw("harmoniczne", "PL1D/harmonic", result.pl1d.energyEv, result.pl1d.harmonic);
		draw("fonon 1D", "PL1D/phonon1d", result.pl1d.energyEv, result.pl1d.phonon1d);
		if (s.ccdComparePhononPl && m_Session->Result())
		{
			const auto &phonons = *m_Session->Result();
			const auto path = PhononPanelSession::DirectoryLabel(s.resultDirectory, m_ProjectDirectory, m_ProjectRoots);
			draw((phonons.label.empty() ? path : phonons.label) + " (fonony)###phononPL", "PL1D/phonons/" + path,
				phonons.plPhotonEnergyEv, PhononPanelSession::PlCurve(phonons, PhononPanelSession::PlCurveChoice(phonons, s.plZpl)));
		}
		for (const auto &dataset : m_Session->Datasets())
			if (dataset.kind == AnalysisDatasetKind::Ccd && m_Session->IsCompared(dataset.directory) &&
				!PhononPanelSession::SameFilePath(dataset.directory, s.selectedCcdDirectory, m_ProjectDirectory))
			{
				std::string error;
				if (const auto ccd = m_Session->Ccd(dataset.directory, error))
				{
					const auto path = PhononPanelSession::DirectoryLabel(dataset.directory, m_ProjectDirectory, m_ProjectRoots);
					draw(dataset.label + "###" + dataset.directory.Utf8(), "PL1D/" + path,
						ccd->get().pl1d.energyEv, ccd->get().pl1d.numerov);
				}
			}
		ImPlot::EndPlot();
		DrawTexAxisLabels(frame, rect, "E [eV]", "PL 1D [j.u.]");
		m_ResetPlots = false; m_PlotTab = 4;
		if (result.pl1d.numerov.empty() && result.pl1d.harmonic.empty() && result.pl1d.phonon1d.empty())
			ImGui::TextDisabled("Brak widma pl_1d.tsv w tym zestawie.");
		for (const auto &error : errors) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", error.c_str());
	}
}
