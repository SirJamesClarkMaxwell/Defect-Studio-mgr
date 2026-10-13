#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/PhononAnalysisIO.hpp"
#include "IO/CcdAnalysisIO.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"
#include "ScientificRuntime/Python/CcdInputsJob.hpp"
#include "ScientificRuntime/Python/CcdAnalysisJob.hpp"
#include "ScientificRuntime/Python/PhononDispersionJob.hpp"

namespace DefectStudio
{
	Path PhononPanelSession::ccdOutputPath() const
	{
		const auto &settings = Settings();
		if (!settings.ccdOutputDirectory.Empty()) return settings.ccdOutputDirectory;
		if (m_ProjectDirectory.Empty() || settings.excitedStateDirectory.Empty()) return {};
		return m_ProjectDirectory.Join("ccd") / settings.excitedStateDirectory.filename();
	}

	void PhononPanelSession::SubmitCcdInputs()
	{
		if (IsBusy()) return;
		const auto jobs = m_JobSystem.lock();
		if (!jobs)
		{
			m_CcdError = "JobSystem niedostępny.";
			return;
		}
		auto &settings = Settings();
		settings.ccdOutputDirectory = ccdOutputPath();
		m_PendingCcdJob = CreateRef<CcdInputsJob>(settings.groundStateDirectory, settings.excitedStateDirectory,
			settings.ccdOutputDirectory, settings.ccdStep);
		m_PendingCcdJobId = jobs->Submit(m_PendingCcdJob, JobPriority::Normal);
		m_CcdInputsResult.clear();
		m_CcdError.clear();
		Changed();
	}

	void PhononPanelSession::pollCcdJob()
	{
		if (!m_PendingCcdJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingCcdJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_PendingCcdJob->GetResult())
			m_CcdInputsResult = *m_PendingCcdJob->GetResult();
		else if (snapshot && snapshot->status == JobStatus::Failed && m_PendingCcdJob->GetError())
			m_CcdError = m_PendingCcdJob->GetError()->userMessage;
		else
			m_CcdError = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Generowanie CCD przerwane.";
		m_PendingCcdJob.reset();
		m_PendingCcdJobId = 0;
	}

	Path PhononPanelSession::ccdAnalysisOutputPath() const
	{
		const auto &s = Settings();
		if (!s.ccdAnalysisOutputDirectory.Empty()) return s.ccdAnalysisOutputDirectory;
		std::vector<Path> inputs;
		if (!s.ccdDirectory.Empty()) inputs.push_back(s.ccdDirectory);
		else if (!s.groundStateDirectory.Empty()) inputs = {s.groundStateDirectory, s.excitedStateDirectory, s.tripletDirectory,
			s.groundAtExcitedDirectory, s.excitedAtGroundDirectory};
		if (s.ccdPhononCurvature) inputs.push_back(s.resultDirectory);
		return defaultDatasetPath(inputs, "ccd");
	}

	std::string PhononPanelSession::ccdAnalysisBlocker() const
	{
		const auto &s = Settings();
		if (m_JobSystem.expired()) return "JobSystem niedostępny.";
		if (s.ccdPhononCurvature && (!Result() || s.resultDirectory.Empty()))
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

	bool PhononPanelSession::loadCcdResult(const Path &directory)
	{
		CcdAnalysisResult result;
		if (!CcdAnalysisIO::Load(directory, result, m_CcdAnalysisError)) return false;
		AddDataset({AnalysisDatasetKind::Ccd, directory,
			result.label.empty() ? directory.filename().Utf8() : result.label, "ds.ccd_analysis.v1"});
		SetCcdResult(std::move(result));
		(void)SelectDataset(directory);
		return true;
	}

	void PhononPanelSession::SubmitCcdAnalysis()
	{
		if (IsBusy()) return;
		m_CcdAnalysisError = ccdAnalysisBlocker();
		if (!m_CcdAnalysisError.empty()) return;
		const auto jobs = m_JobSystem.lock();
		if (!jobs) { m_CcdAnalysisError = "JobSystem niedostępny."; return; }
		const auto &s = Settings();
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
		Changed();
	}

	void PhononPanelSession::pollCcdAnalysisJob()
	{
		if (!m_PendingCcdAnalysisJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingCcdAnalysisJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_PendingCcdAnalysisJob->GetResult())
		{
			if (loadCcdResult(m_PendingCcdAnalysisJob->GetResult()->outputDirectory)) Changed();
		}
		else m_CcdAnalysisError = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie CCD przerwane.";
		m_PendingCcdAnalysisJob.reset(); m_PendingCcdAnalysisJobId = 0;
	}
}
