#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <cmath>
#include <imgui.h>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "IO/CcdAnalysisIO.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"
#include "ScientificRuntime/Python/CcdInputsJob.hpp"
#include "ScientificRuntime/Python/CcdAnalysisJob.hpp"

namespace DefectStudio
{
	Path PhononModesPanel::ccdOutputPath() const
	{
		const auto &settings = m_Session->Settings();
		if (!settings.ccdOutputDirectory.Empty()) return settings.ccdOutputDirectory;
		if (m_ProjectDirectory.Empty() || settings.excitedStateDirectory.Empty()) return {};
		return m_ProjectDirectory.Join("ccd") / settings.excitedStateDirectory.filename();
	}

	void PhononModesPanel::submitCcd()
	{
		const auto jobs = m_JobSystem.lock();
		if (!jobs)
		{
			m_CcdError = "JobSystem niedostępny.";
			return;
		}
		auto &settings = m_Session->Settings();
		settings.ccdOutputDirectory = ccdOutputPath();
		m_PendingCcdJob = CreateRef<CcdInputsJob>(settings.groundStateDirectory, settings.excitedStateDirectory,
			settings.ccdOutputDirectory, settings.ccdStep);
		m_PendingCcdJobId = jobs->Submit(m_PendingCcdJob, JobPriority::Normal);
		m_CcdResult.clear();
		m_CcdError.clear();
		persist();
	}

	void PhononModesPanel::pollCcdJob()
	{
		if (!m_PendingCcdJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingCcdJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_PendingCcdJob->GetResult())
			m_CcdResult = *m_PendingCcdJob->GetResult();
		else if (snapshot && snapshot->status == JobStatus::Failed && m_PendingCcdJob->GetError())
			m_CcdError = m_PendingCcdJob->GetError()->userMessage;
		else
			m_CcdError = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Generowanie CCD przerwane.";
		m_PendingCcdJob.reset();
		m_PendingCcdJobId = 0;
	}

	Path PhononModesPanel::ccdAnalysisOutputPath() const
	{
		const auto &s = m_Session->Settings();
		if (!s.ccdAnalysisOutputDirectory.Empty()) return s.ccdAnalysisOutputDirectory;
		std::vector<Path> inputs;
		if (!s.ccdDirectory.Empty()) inputs.push_back(s.ccdDirectory);
		else if (!s.groundStateDirectory.Empty()) inputs = {s.groundStateDirectory, s.excitedStateDirectory, s.tripletDirectory,
			s.groundAtExcitedDirectory, s.excitedAtGroundDirectory};
		if (s.ccdPhononCurvature) inputs.push_back(s.resultDirectory);
		return defaultDatasetPath(inputs, "ccd");
	}

	std::string PhononModesPanel::ccdAnalysisBlocker() const
	{
		const auto &s = m_Session->Settings();
		if (m_JobSystem.expired()) return "JobSystem niedostępny.";
		if (s.ccdPhononCurvature && (!m_Session->Result() || s.resultDirectory.Empty()))
			return "Wczytaj aktywny zestaw fononów dla krzywizny.";
		std::vector<Path> inputs;
		const bool points = !s.groundStateDirectory.Empty() || !s.excitedStateDirectory.Empty();
		if ((!s.ccdDirectory.Empty() || !points) &&
			(s.zplFormula.find("E_gs") != std::string::npos || s.zplFormula.find("E_es") != std::string::npos || s.zplFormula.find("E_T") != std::string::npos))
			return "Wzór z energiami wymaga punktów GS/ES. Dla ścieżki CCD / samego modu fononowego podaj liczbę ZPL lub zostaw wzór pusty.";
		if (!s.ccdDirectory.Empty()) inputs.push_back(s.ccdDirectory);
		else if (points)
		{
			if (s.groundStateDirectory.Empty() || s.excitedStateDirectory.Empty()) return "Ustaw GS i ES w sekcji Wejścia lub katalog ścieżki CCD.";
			if (s.groundAtExcitedDirectory.Empty() && s.excitedAtGroundDirectory.Empty() && !s.ccdPhononCurvature)
				return "Brak krzywizny: ustaw E_gs@es / E_es@gs lub włącz krzywiznę z fononów.";
			inputs = {s.groundStateDirectory, s.excitedStateDirectory, s.tripletDirectory,
				s.groundAtExcitedDirectory, s.excitedAtGroundDirectory};
		}
		else if (!s.ccdPhononCurvature) return "Ustaw GS/ES, ścieżkę CCD lub krzywiznę z aktywnych fononów.";
		if (s.ccdPhononCurvature) inputs.push_back(s.resultDirectory);
		const auto output = ccdAnalysisOutputPath();
		if (output.Empty()) return "Ustaw katalog projektu lub katalog wyników CCD.";
		for (const auto &input : inputs)
		{
			if (input.Empty()) continue;
			std::error_code error;
			if (!std::filesystem::is_directory(input.Native(), error)) return "Katalog wejściowy nie istnieje: " + input.Utf8();
			if (PhononPanelSession::OutputContainsInput(output, input)) return "Katalog wyników nie może być wejściem ani go zawierać.";
		}
		if (!std::isfinite(s.sigmaMeV) || s.sigmaMeV <= 0 || !std::isfinite(s.ccdTemperatureK) || s.ccdTemperatureK < 0)
			return "sigma musi być dodatnia, a T nieujemne.";
		return {};
	}

	bool PhononModesPanel::loadCcdResult(const Path &directory)
	{
		CcdAnalysisResult result;
		if (!CcdAnalysisIO::Load(directory, result, m_CcdAnalysisError)) return false;
		m_Session->AddDataset({AnalysisDatasetKind::Ccd, directory,
			result.label.empty() ? directory.filename().Utf8() : result.label, "ds.ccd_analysis.v1"});
		m_Session->SetCcdResult(std::move(result));
		(void)m_Session->SelectDataset(directory);
		m_ResetPlots = true;
		return true;
	}

	void PhononModesPanel::submitCcdAnalysis()
	{
		m_CcdAnalysisError = ccdAnalysisBlocker();
		if (!m_CcdAnalysisError.empty()) return;
		const auto jobs = m_JobSystem.lock();
		if (!jobs) { m_CcdAnalysisError = "JobSystem niedostępny."; return; }
		const auto &s = m_Session->Settings();
		CcdAnalysisRequest request;
		request.ccdDir = s.ccdDirectory;
		if (s.ccdDirectory.Empty() && !s.groundStateDirectory.Empty())
		{
			request.gsDir = s.groundStateDirectory; request.esDir = s.excitedStateDirectory;
			request.tripletDir = s.tripletDirectory;
			request.gsAtEsDir = s.groundAtExcitedDirectory; request.esAtGsDir = s.excitedAtGroundDirectory;
		}
		request.zplFormula = s.zplFormula;
		if (s.ccdPhononCurvature) request.phononResultDir = s.resultDirectory;
		request.sigmaMeV = s.sigmaMeV; request.temperatureK = s.ccdTemperatureK;
		request.label = PhononPanelSession::DirectoryLabel(!s.ccdDirectory.Empty() ? s.ccdDirectory :
			!s.excitedStateDirectory.Empty() ? s.excitedStateDirectory : s.resultDirectory,
			m_ProjectDirectory, m_ProjectRoots);
		request.outputDirectory = ccdAnalysisOutputPath();
		m_PendingCcdAnalysisJob = CreateRef<CcdAnalysisJob>(std::move(request));
		m_PendingCcdAnalysisJobId = jobs->Submit(m_PendingCcdAnalysisJob, JobPriority::Normal);
		persist();
	}

	void PhononModesPanel::pollCcdAnalysisJob()
	{
		if (!m_PendingCcdAnalysisJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingCcdAnalysisJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_PendingCcdAnalysisJob->GetResult())
		{
			if (loadCcdResult(m_PendingCcdAnalysisJob->GetResult()->outputDirectory)) persist();
		}
		else m_CcdAnalysisError = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie CCD przerwane.";
		m_PendingCcdAnalysisJob.reset(); m_PendingCcdAnalysisJobId = 0;
	}

	void PhononModesPanel::drawCcd()
	{
		if (!ImGui::CollapsingHeader("CCD / PL 1D")) return;
		auto &s = m_Session->Settings();
		const bool busy = m_PendingJob || m_PendingCcdJob || m_PendingDispersionJob || m_PendingCcdAnalysisJob;
		ImGui::TextDisabled("GS, ES, tryplet, dodatkowe energie i ZPL: sekcja Wejścia.");
		ImGui::BeginDisabled(busy);
		{
			PropertyGrid grid("##ccdAnalysisInputs", PropertyGridLabelWidth({"Ścieżka CCD (opcjonalna)", "Wyniki CCD", "sigma [meV]", "T [K]"}));
			if (grid)
			{
				grid.Row("Ścieżka CCD (opcjonalna)");
				if (DrawFolder("ccdPath", s.ccdDirectory, m_CcdAnalysisError)) persist();
				ImGui::SetItemTooltip("Katalog obliczonych punktów gs/es. Pusty = pojedyncze punkty GS/ES z Wejścia.");
				grid.Row("Wyniki CCD");
				if (DrawFolder("ccdAnalysisOutput", s.ccdAnalysisOutputDirectory, m_CcdAnalysisError, true, ccdAnalysisOutputPath())) persist();
				grid.Row("sigma [meV]"); ImGui::InputDouble("##ccdSigma", &s.sigmaMeV, 0, 0, "%.4g");
				if (ImGui::IsItemDeactivatedAfterEdit()) { if (!std::isfinite(s.sigmaMeV) || s.sigmaMeV <= 0) s.sigmaMeV = 6; persist(); }
				grid.Row("T [K]"); ImGui::InputDouble("##ccdTemperature", &s.ccdTemperatureK, 0, 0, "%.4g");
				if (ImGui::IsItemDeactivatedAfterEdit()) { if (!std::isfinite(s.ccdTemperatureK) || s.ccdTemperatureK < 0) s.ccdTemperatureK = 0; persist(); }
			}
		}
		if (ImGui::Checkbox("krzywizna z fononów", &s.ccdPhononCurvature)) persist();
		ImGui::SetItemTooltip("Aktywny zestaw fononów → mod efektywny 1D. Krzywizna z dodatkowych energii ma pierwszeństwo.");
		const auto blocker = ccdAnalysisBlocker();
		ImGui::BeginDisabled(!blocker.empty());
		if (ImGui::Button("Policz CCD / PL 1D")) submitCcdAnalysis();
		ImGui::EndDisabled();
		if (!blocker.empty()) ImGui::SetItemTooltip("%s", blocker.c_str());
		else if (busy) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		ImGui::SameLine();
		if (ImGui::Button("Wczytaj CCD..."))
		{
			const auto picked = Platform::PickFolder(s.selectedCcdDirectory);
			if (!picked) m_CcdAnalysisError = picked.Error().userMessage;
			else if (picked->has_value() && loadCcdResult(**picked)) persist();
		}
		ImGui::EndDisabled();
		if (!blocker.empty()) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%s", blocker.c_str());
		if (m_PendingCcdAnalysisJob)
		{
			const auto jobs = m_JobSystem.lock();
			const auto snapshot = jobs ? jobs->GetJob(m_PendingCcdAnalysisJobId) : std::nullopt;
			if (snapshot) ImGui::ProgressBar(snapshot->totalWork > 0 ? snapshot->completedWork / snapshot->totalWork : 0,
				ImVec2(-1, 0), snapshot->currentMessage.c_str());
			if (ImGui::Button("Anuluj analizę CCD") && jobs) (void)jobs->RequestCancel(m_PendingCcdAnalysisJobId);
		}
		if (!m_CcdAnalysisError.empty()) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", m_CcdAnalysisError.c_str());
		if (ImGui::TreeNode("Generuj punkty CCD"))
		{
			ImGui::BeginDisabled(busy);
			{
				PropertyGrid grid("##ccdInputs", PropertyGridLabelWidth({"Katalog punktów CCD", "Krok t"}));
				if (grid)
				{
					grid.Row("Katalog punktów CCD");
					if (DrawFolder("ccd", s.ccdOutputDirectory, m_CcdError, true, ccdOutputPath())) persist();
					grid.Row("Krok t"); ImGui::InputDouble("##ccdStep", &s.ccdStep, 0, 0, "%.4g");
					if (ImGui::IsItemDeactivatedAfterEdit()) { if (!std::isfinite(s.ccdStep) || s.ccdStep <= 0) s.ccdStep = 0.25; persist(); }
				}
			}
			const std::string reason = s.groundStateDirectory.Empty() || s.excitedStateDirectory.Empty() ? "Ustaw GS i ES w sekcji Wejścia." :
				ccdOutputPath().Empty() ? "Ustaw katalog punktów CCD lub katalog projektu." : "";
			ImGui::BeginDisabled(!reason.empty());
			if (ImGui::Button("Generuj punkty CCD")) submitCcd();
			ImGui::EndDisabled();
			if (!reason.empty()) ImGui::SetItemTooltip("%s", reason.c_str());
			else if (busy) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
			ImGui::EndDisabled();
			if (!reason.empty()) ImGui::TextDisabled("%s", reason.c_str());
			ImGui::TextDisabled("Przed sbatch: WAVECAR do es/t_+100 i gs/t_+000");
			ImGui::TreePop();
		}
		if (m_PendingCcdJob)
		{
			ImGui::TextDisabled("Generowanie punktów CCD...");
			if (ImGui::Button("Anuluj punkty CCD"))
				if (const auto jobs = m_JobSystem.lock()) (void)jobs->RequestCancel(m_PendingCcdJobId);
		}
		if (!m_CcdResult.empty()) ImGui::TextUnformatted(m_CcdResult.c_str());
		if (!m_CcdError.empty()) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", m_CcdError.c_str());
	}
}
