#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <nlohmann/json.hpp>
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
		return CaseOutputDirectory("ccd_inputs");
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
		m_PendingCcdInputsCaseId = ActiveCaseId();
		settings.ccdOutputDirectory = ccdOutputPath();
		AttachDataset(ActiveCaseId(), settings.ccdOutputDirectory);
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
		{
			if (m_PendingCcdInputsCaseId == ActiveCaseId()) m_CcdInputsResult = *m_PendingCcdJob->GetResult();
			try
			{
				const auto result = nlohmann::json::parse(*m_PendingCcdJob->GetResult());
				AttachDataset(m_PendingCcdInputsCaseId, Path::FromUtf8(result.at("out_dir").get<std::string>()));
				Changed();
			}
			catch (const std::exception &exception) { if (m_PendingCcdInputsCaseId == ActiveCaseId()) m_CcdError = exception.what(); }
		}
		else if (m_PendingCcdInputsCaseId == ActiveCaseId() && snapshot && snapshot->status == JobStatus::Failed && m_PendingCcdJob->GetError())
			m_CcdError = m_PendingCcdJob->GetError()->userMessage;
		else if (m_PendingCcdInputsCaseId == ActiveCaseId())
			m_CcdError = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Generowanie CCD przerwane.";
		m_PendingCcdInputsCaseId.clear();
		m_PendingCcdJob.reset();
		m_PendingCcdJobId = 0;
	}

	Path PhononPanelSession::ccdAnalysisOutputPath() const
	{
		return ccdAnalysisOutputPath(Settings(), ActiveCaseId());
	}

	bool PhononPanelSession::loadCcdResult(const Path &directory)
	{
		CcdAnalysisResult result;
		if (!CcdAnalysisIO::Load(directory, result, m_CcdAnalysisError)) return false;
		if (auto ref = ReadDatasetRef(directory)) AddDataset(std::move(*ref));
		else AddDataset({AnalysisDatasetKind::Ccd, directory,
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
		submitCcdAnalysis(ccdAnalysisRequest(Settings(), ActiveCaseId()), ActiveCaseId());
	}

	void PhononPanelSession::submitCcdAnalysis(CcdAnalysisRequest request, const std::string &caseId)
	{
		m_PendingCcdAnalysisCaseId = caseId;
		auto &run = m_CaseRuns[caseId];
		run.status = PhononCaseStatus::Running;
		run.output = request.outputDirectory;
		if (!m_BatchEntry) run.message.clear();
		AttachDataset(caseId, request.outputDirectory);
		m_PendingCcdAnalysisJob = CreateRef<CcdAnalysisJob>(std::move(request));
		m_PendingCcdAnalysisJobId = submitCaseJob(m_PendingCcdAnalysisJob);
		Changed();
	}

	void PhononPanelSession::pollCcdAnalysisJob()
	{
		if (!m_PendingCcdAnalysisJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingCcdAnalysisJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		const bool completed = snapshot && snapshot->status == JobStatus::Completed && m_PendingCcdAnalysisJob->GetResult();
		const std::string caseId = std::exchange(m_PendingCcdAnalysisCaseId, {});
		finishCaseJob(PhononBatchStep::CcdAnalysis, caseId,
			completed ? m_PendingCcdAnalysisJob->GetResult()->outputDirectory : CaseRun(caseId).output,
			completed ? JobStatus::Completed : snapshot && snapshot->status == JobStatus::Cancelled ? JobStatus::Cancelled : JobStatus::Failed,
			completed ? std::string{} : snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie CCD przerwane.");
		m_PendingCcdAnalysisJob.reset(); m_PendingCcdAnalysisJobId = 0;
	}
}
