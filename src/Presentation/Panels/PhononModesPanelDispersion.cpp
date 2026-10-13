#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <span>
#include <imgui.h>
#include <implot.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"
#include "Presentation/Panels/PlotWidgets.hpp"
#include "ScientificRuntime/Python/PhononDispersionJob.hpp"

namespace DefectStudio
{
	Path PhononModesPanel::dispersionOutputPath() const
	{
		const auto &settings = m_Session->Settings();
		if (!settings.dispersionDirectory.Empty()) return settings.dispersionDirectory;
		return defaultDatasetPath({settings.bulkForceConstantDirectory}, "dispersion");
	}

	bool PhononModesPanel::loadDispersion(const Path &directory)
	{
		if (auto ref = ReadDatasetRef(directory)) m_Session->AddDataset(std::move(*ref));
		PhononDispersion result;
		if (!PhononDispersionIO::Load(directory, result, m_DispersionError)) return false;
		m_Dispersion = std::move(result);
		(void)m_Session->SelectDataset(directory);
		m_ResetDispersionPlot = true;
		return true;
	}

	void PhononModesPanel::submitDispersion()
	{
		const auto jobs = m_JobSystem.lock();
		if (!jobs) { m_DispersionError = "JobSystem niedostępny."; return; }
		auto &settings = m_Session->Settings();
		m_PendingDispersionJob = CreateRef<PhononDispersionJob>(settings.bulkForceConstantDirectory,
			dispersionOutputPath(), settings.dispersionInPlane, settings.dispersionPoints);
		m_PendingDispersionJobId = jobs->Submit(m_PendingDispersionJob, JobPriority::Normal);
		m_DispersionError.clear();
		persist();
	}

	void PhononModesPanel::pollDispersionJob()
	{
		if (!m_PendingDispersionJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingDispersionJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_PendingDispersionJob->GetResult())
		{
			if (loadDispersion(*m_PendingDispersionJob->GetResult())) persist();
		}
		else if (snapshot && snapshot->status == JobStatus::Failed && m_PendingDispersionJob->GetError())
			m_DispersionError = m_PendingDispersionJob->GetError()->userMessage;
		else
			m_DispersionError = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie dyspersji przerwane.";
		m_PendingDispersionJob.reset();
		m_PendingDispersionJobId = 0;
	}

	void PhononModesPanel::drawDispersion()
	{
		if (!ImGui::CollapsingHeader("Dyspersja bulku")) return;
		auto &settings = m_Session->Settings();
		ImGui::BeginDisabled(m_PendingJob != nullptr || m_PendingCcdJob != nullptr || m_PendingDispersionJob != nullptr);
		{
			PropertyGrid grid("##dispersionInputs", PropertyGridLabelWidth({"Katalog FC bulku", "Katalog wyników (opcjonalny)", "npoints"}));
			if (grid)
			{
				grid.Row("Katalog FC bulku");
				if (DrawFolder("dispersionBulk", settings.bulkForceConstantDirectory, m_DispersionError)) persist();
				grid.Row("Katalog wyników (opcjonalny)");
				if (DrawFolder("dispersionOutput", settings.dispersionDirectory, m_DispersionError, true, dispersionOutputPath()))
				{
					persist();
				}
				grid.Row("npoints");
				ImGui::InputInt("##dispersionPoints", &settings.dispersionPoints);
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					settings.dispersionPoints = std::max(2, settings.dispersionPoints);
					persist();
				}
			}
		}
		if (ImGui::Checkbox("tylko w płaszczyźnie (2D)", &settings.dispersionInPlane)) persist();
		const std::string blocker = settings.bulkForceConstantDirectory.Empty() ? "Ustaw katalog FC bulku." :
			dispersionOutputPath().Empty() ? "Ustaw katalog projektu lub katalog wyników." :
			PhononPanelSession::OutputContainsInput(dispersionOutputPath(), settings.bulkForceConstantDirectory) ?
			"Katalog wyników nie może być katalogiem wejściowym ani go zawierać." : "";
		ImGui::BeginDisabled(!blocker.empty());
		if (ImGui::Button("Policz dyspersję")) submitDispersion();
		ImGui::EndDisabled();
		if (!blocker.empty()) ImGui::SetItemTooltip("%s", blocker.c_str());
		else if (m_PendingJob || m_PendingCcdJob || m_PendingDispersionJob) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		ImGui::SameLine();
		if (ImGui::Button("Wczytaj..."))
		{
			const auto picked = Platform::PickFolder(dispersionOutputPath());
			if (!picked) m_DispersionError = picked.Error().userMessage;
			else if (picked->has_value() && loadDispersion(**picked)) persist();
		}
		ImGui::EndDisabled();
		if (m_PendingJob || m_PendingCcdJob || m_PendingDispersionJob) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		if (m_PendingDispersionJob)
		{
			const auto jobs = m_JobSystem.lock();
			const auto snapshot = jobs ? jobs->GetJob(m_PendingDispersionJobId) : std::nullopt;
			if (snapshot)
				ImGui::ProgressBar(snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f,
					ImVec2(-1.0f, 0.0f), snapshot->currentMessage.c_str());
			if (ImGui::Button("Anuluj dyspersję") && jobs) (void)jobs->RequestCancel(m_PendingDispersionJobId);
		}
		if (!m_DispersionError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_DispersionError.c_str());
		if (!m_Dispersion) return;
		const auto selectedLabel = PhononPanelSession::DirectoryLabel(settings.selectedDispersionDirectory, m_ProjectDirectory, m_ProjectRoots);
		ImGui::TextUnformatted(selectedLabel.c_str());
		ImGui::SetItemTooltip("%s", settings.selectedDispersionDirectory.Utf8().c_str());
		const auto &result = *m_Dispersion;
		ImGui::Text("pasm: %d, max ℏω = %.2f meV", result.bandCount, result.maxMeV);
		if (!ImPlot::BeginPlot("##bulkDispersion", ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 18.0f))) return;
		ImPlot::SetupAxes(nullptr, "ℏω [meV]", ImPlotAxisFlags_NoGridLines, 0);
		const auto condition = m_ResetDispersionPlot ? ImPlotCond_Always : ImPlotCond_Once;
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
		m_ResetDispersionPlot = false;
	}
}
