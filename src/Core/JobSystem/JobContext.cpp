#include "Core/dspch.hpp"

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	JobContext::JobContext(
		ProgressCallback progressCallback,
		StageCallback stageCallback,
		MessageCallback messageCallback,
		LogCallback logCallback,
		CancelQuery cancelQuery,
		SubmitJobCallback submitJobCallback,
		WaitJobCallback waitJobCallback)
		: m_ProgressCallback(std::move(progressCallback)),
		m_StageCallback(std::move(stageCallback)),
		m_MessageCallback(std::move(messageCallback)),
		m_LogCallback(std::move(logCallback)),
		m_CancelQuery(std::move(cancelQuery)),
		m_SubmitJobCallback(std::move(submitJobCallback)),
		m_WaitJobCallback(std::move(waitJobCallback))
	{
	}

	void JobContext::SetProgress(float completedWork, float totalWork) const
	{
		if (m_ProgressCallback)
			m_ProgressCallback(completedWork, totalWork);
	}

	void JobContext::SetStage(const std::string &stage) const
	{
		if (m_StageCallback)
			m_StageCallback(stage);
	}

	void JobContext::SetMessage(const std::string &message) const
	{
		if (m_MessageCallback)
			m_MessageCallback(message);
	}

	void JobContext::LogDebug(const std::string &message) const
	{
		if (m_LogCallback)
			m_LogCallback(JobLogLevel::Debug, message);
	}

	void JobContext::LogInfo(const std::string &message) const
	{
		if (m_LogCallback)
			m_LogCallback(JobLogLevel::Info, message);
	}

	void JobContext::LogWarning(const std::string &message) const
	{
		if (m_LogCallback)
			m_LogCallback(JobLogLevel::Warning, message);
	}

	void JobContext::LogError(const std::string &message) const
	{
		if (m_LogCallback)
			m_LogCallback(JobLogLevel::Error, message);
	}

	bool JobContext::IsCancellationRequested() const
	{
		return m_CancelQuery ? m_CancelQuery() : false;
	}

	void JobContext::ThrowIfCancellationRequested() const
	{
		if (IsCancellationRequested())
			throw JobCancelledException();
	}

	JobId JobContext::SubmitJob(const Ref<IJob> &job, JobPriority priority) const
	{
		if (m_SubmitJobCallback)
			return m_SubmitJobCallback(job, priority);
		return 0; // Failed submission
	}

	bool JobContext::WaitForJob(JobId id) const
	{
		if (m_WaitJobCallback)
			return m_WaitJobCallback(id);
		return false;
	}

	JobId JobContext::SubmitJobSequential(const Ref<IJob> &job, JobPriority priority) const
	{
		const JobId id = SubmitJob(job, priority);
		if (id == 0)
			return 0;

		if (!WaitForJob(id))
			return 0;

		return id;
	}
} // namespace DefectStudio
