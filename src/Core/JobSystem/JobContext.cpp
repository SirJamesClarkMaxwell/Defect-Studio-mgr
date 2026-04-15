#include "Core/dspch.hpp"

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	JobContext::JobContext(
		ProgressCallback progressCallback,
		StageCallback stageCallback,
		MessageCallback messageCallback,
		LogCallback logCallback,
		CancelQuery cancelQuery)
		: m_ProgressCallback(std::move(progressCallback)),
		  m_StageCallback(std::move(stageCallback)),
		  m_MessageCallback(std::move(messageCallback)),
		  m_LogCallback(std::move(logCallback)),
		  m_CancelQuery(std::move(cancelQuery))
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
} // namespace DefectStudio
