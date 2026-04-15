#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"

namespace DefectStudio
{
	class JobContext
	{
	public:
		using ProgressCallback = std::function<void(float completedWork, float totalWork)>;
		using StageCallback = std::function<void(const std::string &stage)>;
		using MessageCallback = std::function<void(const std::string &message)>;
		using LogCallback = std::function<void(JobLogLevel level, const std::string &message)>;
		using CancelQuery = std::function<bool()>;
		using SubmitJobCallback = std::function<JobId(const Ref<IJob>&, JobPriority)>;

		JobContext(
			ProgressCallback progressCallback,
			StageCallback stageCallback,
			MessageCallback messageCallback,
			LogCallback logCallback,
			CancelQuery cancelQuery,
			SubmitJobCallback submitJobCallback = {});

		void SetProgress(float completedWork, float totalWork) const;
		void SetStage(const std::string &stage) const;
		void SetMessage(const std::string &message) const;

		void LogDebug(const std::string &message) const;
		void LogInfo(const std::string &message) const;
		void LogWarning(const std::string &message) const;
		void LogError(const std::string &message) const;

		[[nodiscard]] bool IsCancellationRequested() const;
		void ThrowIfCancellationRequested() const;

		[[nodiscard]] JobId SubmitJob(const Ref<IJob> &job, JobPriority priority = JobPriority::Normal) const;

	private:
		ProgressCallback m_ProgressCallback;
		StageCallback m_StageCallback;
		MessageCallback m_MessageCallback;
		LogCallback m_LogCallback;
		CancelQuery m_CancelQuery;
		SubmitJobCallback m_SubmitJobCallback;
	};
} // namespace DefectStudio
