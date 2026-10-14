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
	void PhononPanelSession::cancelJob(JobId id)
	{
		if (const auto jobs = m_JobSystem.lock()) (void)jobs->RequestCancel(id);
	}

	void PhononPanelSession::cancelCaseJobs()
	{
		if (m_PendingJob) cancelJob(m_PendingJobId);
		m_PendingJob.reset();
		m_PendingJobId = 0;
		if (m_PendingCcdJob) cancelJob(m_PendingCcdJobId);
		m_PendingCcdJob.reset();
		m_PendingCcdJobId = 0;
		if (m_PendingCcdAnalysisJob) cancelJob(m_PendingCcdAnalysisJobId);
		m_PendingCcdAnalysisJob.reset(); m_PendingCcdAnalysisJobId = 0;
	}

	void PhononPanelSession::cancelJob()
	{
		CancelBatch();
		cancelCaseJobs();
		m_BatchQueue.clear();
		m_BatchEntry.reset();
		m_BatchCancelling = false;
		m_PendingCaseId.clear();
		m_PendingCcdInputsCaseId.clear();
		m_PendingCcdAnalysisCaseId.clear();
		if (m_PendingDispersionJob) cancelJob(m_PendingDispersionJobId);
		m_PendingDispersionJob.reset();
		m_PendingDispersionJobId = 0;
	}

	void PhononPanelSession::SubmitComputation()
	{
		if (IsBusy()) return;
		const auto jobs = m_JobSystem.lock();
		if (!jobs)
		{
			m_Error = "JobSystem niedostępny.";
			return;
		}
		m_Error = ComputationBlocker();
		if (!m_Error.empty()) return;
		submitComputation(computationRequest(Settings(), ActiveCaseId()), ActiveCaseId());
	}

	void PhononPanelSession::submitComputation(PhononAnalysisRequest request, const std::string &caseId)
	{
		m_PendingCaseId = caseId;
		auto &run = m_CaseRuns[caseId];
		run.status = PhononCaseStatus::Running;
		run.output = request.outputDirectory;
		if (!m_BatchEntry) run.message.clear();
		AttachDataset(caseId, request.outputDirectory);
		m_PendingJob = CreateRef<PhononAnalysisJob>(std::move(request));
		m_PendingJobId = submitCaseJob(m_PendingJob);
		if (caseId == ActiveCaseId()) m_Error.clear();
		Changed();
	}

	void PhononPanelSession::pollJob()
	{
		if (!m_PendingJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		const bool completed = snapshot && snapshot->status == JobStatus::Completed && m_PendingJob->GetResult();
		const std::string caseId = std::exchange(m_PendingCaseId, {});
		finishCaseJob(PhononBatchStep::Phonons, caseId,
			completed ? m_PendingJob->GetResult()->outputDirectory : CaseRun(caseId).output,
			completed ? JobStatus::Completed : snapshot && snapshot->status == JobStatus::Cancelled ? JobStatus::Cancelled : JobStatus::Failed,
			completed ? std::string{} : snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie fononów przerwane.");
		m_PendingJob.reset();
		m_PendingJobId = 0;
	}

	Path PhononPanelSession::dispersionOutputPath() const
	{
		const auto &settings = Settings();
		if (!settings.dispersionDirectory.Empty()) return settings.dispersionDirectory;
		return defaultDatasetPath({settings.bulkForceConstantDirectory}, "dispersion");
	}

	bool PhononPanelSession::loadDispersion(const Path &directory)
	{
		if (auto ref = ReadDatasetRef(directory)) AddDataset(std::move(*ref));
		PhononDispersion result;
		if (!PhononDispersionIO::Load(directory, result, m_DispersionError)) return false;
		m_Dispersion = std::move(result);
		++m_PlotRevision;
		(void)SelectDataset(directory);
		return true;
	}

	void PhononPanelSession::SubmitDispersion()
	{
		if (IsBusy()) return;
		const auto jobs = m_JobSystem.lock();
		if (!jobs) { m_DispersionError = "JobSystem niedostępny."; return; }
		auto &settings = Settings();
		m_PendingDispersionJob = CreateRef<PhononDispersionJob>(settings.bulkForceConstantDirectory,
			dispersionOutputPath(), settings.dispersionInPlane, settings.dispersionPoints);
		m_PendingDispersionJobId = jobs->Submit(m_PendingDispersionJob, JobPriority::Normal);
		m_DispersionError.clear();
		Changed();
	}

	void PhononPanelSession::pollDispersionJob()
	{
		if (!m_PendingDispersionJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingDispersionJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_PendingDispersionJob->GetResult())
		{
			if (loadDispersion(*m_PendingDispersionJob->GetResult())) Changed();
		}
		else if (snapshot && snapshot->status == JobStatus::Failed && m_PendingDispersionJob->GetError())
			m_DispersionError = m_PendingDispersionJob->GetError()->userMessage;
		else
			m_DispersionError = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie dyspersji przerwane.";
		m_PendingDispersionJob.reset();
		m_PendingDispersionJobId = 0;
	}
}
