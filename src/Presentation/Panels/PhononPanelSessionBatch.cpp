#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include "Core/JobSystem/JobSystem.hpp"
#include "IO/PhononAnalysisIO.hpp"
#include "IO/CcdAnalysisIO.hpp"

namespace DefectStudio
{
	const PhononCaseRun &PhononPanelSession::CaseRun(const std::string &caseId) const
	{
		static const PhononCaseRun idle;
		const auto found = m_CaseRuns.find(caseId);
		return found == m_CaseRuns.end() ? idle : found->second;
	}

	bool PhononPanelSession::IsBatchRunning() const
	{
		return m_BatchEntry.has_value() || !m_BatchQueue.empty();
	}

	std::pair<std::size_t, std::size_t> PhononPanelSession::BatchProgress() const
	{
		return {m_BatchDone, m_BatchTotal};
	}

	void PhononPanelSession::StartBatch(std::vector<std::string> caseIds, std::vector<PhononBatchStep> steps)
	{
		if (IsBusy() || steps.empty()) return;
		const bool phonons = std::ranges::find(steps, PhononBatchStep::Phonons) != steps.end();
		const bool ccd = std::ranges::find(steps, PhononBatchStep::CcdAnalysis) != steps.end();
		if (!phonons && !ccd) return;
		(void)Cases();
		m_BatchDone = m_BatchTotal = 0;
		m_BatchCancelling = false;
		std::vector<std::string> seen;
		for (const auto &id : caseIds)
		{
			if (std::ranges::find(seen, id) != seen.end()) continue;
			seen.push_back(id);
			const auto found = std::ranges::find(m_Settings.cases, id, &PhononCase::id);
			if (found == m_Settings.cases.end()) continue;
			const auto settings = found->settings;
			const std::size_t count = static_cast<std::size_t>(phonons) + static_cast<std::size_t>(ccd);
			m_BatchTotal += count;
			std::string blocker = phonons ? ComputationBlocker(settings, id) : std::string{};
			auto ccdSettings = settings;
			const auto phononRequest = phonons ? computationRequest(settings, id) : PhononAnalysisRequest{};
			if (phonons && ccdSettings.ccdPhononCurvature) ccdSettings.resultDirectory = phononRequest.outputDirectory;
			if (blocker.empty() && ccd) blocker = ccdAnalysisBlocker(ccdSettings, id, phonons);
			if (!blocker.empty())
			{
				m_CaseRuns[id] = {PhononCaseStatus::Failed, std::move(blocker), {}};
				m_BatchDone += count;
				continue;
			}
			m_CaseRuns[id] = {PhononCaseStatus::Queued, {}, {}};
			if (phonons)
			{
				m_BatchQueue.push_back({id, phononRequest});
				AttachDataset(id, phononRequest.outputDirectory);
			}
			if (ccd)
			{
				const auto request = ccdAnalysisRequest(ccdSettings, id);
				m_BatchQueue.push_back({id, request});
				AttachDataset(id, request.outputDirectory);
			}
		}
		submitNextBatch();
	}

	void PhononPanelSession::CancelBatch()
	{
		if (!IsBatchRunning()) return;
		m_BatchCancelling = true;
		for (const auto &entry : m_BatchQueue)
			m_CaseRuns[entry.caseId].status = PhononCaseStatus::Cancelled;
		m_BatchDone += m_BatchQueue.size();
		m_BatchQueue.clear();
		if (m_BatchEntry)
		{
			m_CaseRuns[m_BatchEntry->caseId].status = PhononCaseStatus::Cancelled;
			if (m_PendingJob) cancelJob(m_PendingJobId);
			if (m_PendingCcdAnalysisJob) cancelJob(m_PendingCcdAnalysisJobId);
		}
	}

	void PhononPanelSession::submitNextBatch()
	{
		if (m_BatchEntry || m_BatchCancelling || m_BatchQueue.empty()) return;
		m_BatchEntry = std::move(m_BatchQueue.front());
		m_BatchQueue.pop_front();
		const auto &entry = *m_BatchEntry;
		m_CaseRuns[entry.caseId].status = PhononCaseStatus::Running;
		if (std::holds_alternative<PhononAnalysisRequest>(entry.request))
			submitComputation(std::get<PhononAnalysisRequest>(entry.request), entry.caseId);
		else
			submitCcdAnalysis(std::get<CcdAnalysisRequest>(entry.request), entry.caseId);
	}

	JobId PhononPanelSession::submitCaseJob(const Ref<IJob> &job)
	{
		if (m_CaseJobSubmitter) return m_CaseJobSubmitter(job);
		const auto jobs = m_JobSystem.lock();
		return jobs ? jobs->Submit(job, JobPriority::Normal) : 0;
	}

	std::string PhononPanelSession::commitCaseOutput(const std::string &caseId, PhononBatchStep step, const Path &output)
	{
		const auto found = std::ranges::find(m_Settings.cases, caseId, &PhononCase::id);
		// Legacy callers can compute without a case; a removed case must never affect another one.
		if (found == m_Settings.cases.end() && !caseId.empty()) return "Przypadek został usunięty.";
		AttachDataset(caseId, output);
		if (auto dataset = ReadDatasetRef(output)) AddDataset(std::move(*dataset));
		std::string error;
		if (step == PhononBatchStep::Phonons)
		{
			if (caseId == ActiveCaseId())
			{
				loadResult(output);
				if (!m_Error.empty()) return m_Error;
			}
			else
			{
				if (!Spectra(output, error)) return error;
			}
			if (found != m_Settings.cases.end()) found->settings.resultDirectory = output;
		}
		else
		{
			if (caseId == ActiveCaseId())
			{
				if (!loadCcdResult(output)) return m_CcdAnalysisError;
			}
			else
			{
				if (!Ccd(output, error)) return error;
			}
			if (found != m_Settings.cases.end()) found->settings.selectedCcdDirectory = output;
		}
		Changed();
		return {};
	}

	void PhononPanelSession::finishCaseJob(PhononBatchStep step, const std::string &caseId, const Path &output,
		JobStatus status, std::string message)
	{
		if (m_BatchEntry && m_BatchCancelling) status = JobStatus::Cancelled;
		if (status == JobStatus::Completed)
		{
			message = commitCaseOutput(caseId, step, output);
			if (!message.empty()) status = JobStatus::Failed;
		}
		auto &run = m_CaseRuns[caseId];
		const bool failedEarlier = m_BatchEntry && !run.message.empty();
		run.status = status == JobStatus::Completed ? PhononCaseStatus::Done :
			status == JobStatus::Cancelled ? PhononCaseStatus::Cancelled : PhononCaseStatus::Failed;
		if (!failedEarlier) run.message = std::move(message);
		if (failedEarlier && status == JobStatus::Completed) run.status = PhononCaseStatus::Failed;
		run.output = output;
		if (caseId == ActiveCaseId())
		{
			if (step == PhononBatchStep::Phonons) m_Error = run.message;
			else m_CcdAnalysisError = run.message;
		}
		if (m_BatchEntry)
		{
			++m_BatchDone;
			if (status != JobStatus::Completed && step == PhononBatchStep::Phonons)
			{
				// A failed phonon step cannot supply curvature to its CCD step.
				m_BatchDone += std::erase_if(m_BatchQueue, [&](const auto &entry) {
					return entry.caseId == caseId && std::holds_alternative<CcdAnalysisRequest>(entry.request) &&
						SameFilePath(std::get<CcdAnalysisRequest>(entry.request).phononResultDir, output, m_ProjectDirectory);
				});
			}
			if (std::ranges::any_of(m_BatchQueue, [&](const auto &entry) { return entry.caseId == caseId; }))
				run.status = PhononCaseStatus::Queued;
			m_BatchEntry.reset();
		}
	}
}
