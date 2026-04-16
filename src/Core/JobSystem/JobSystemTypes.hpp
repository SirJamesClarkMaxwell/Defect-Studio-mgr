#pragma once

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Time.hpp"

namespace DefectStudio
{
	using JobId = std::uint64_t;

	enum class JobStatus
	{
		Queued,
		Running,
		Completed,
		Failed,
		Cancelled
	};

	enum class JobPriority
	{
		Lowest,
		Low,
		Normal,
		High,
		Highest
	};

	enum class JobLogLevel
	{
		Debug,
		Info,
		Warning,
		Error
	};

	struct JobLogEntry
	{
		JobLogLevel level = JobLogLevel::Info;
		std::string message;
		Time::TimePoint timestamp{};
	};

	struct JobSnapshot
	{
		JobId id = 0;
		JobId parentId = 0;
		std::string name;
		std::string type;
		JobStatus status = JobStatus::Queued;
		JobPriority priority = JobPriority::Normal;
		float totalWork = 0.0f;
		float completedWork = 0.0f;
		std::string currentStage;
		std::string currentMessage;
		std::string errorMessage;
		bool cancellationRequested = false;
		Time::TimePoint createdAt{};
		Time::TimePoint startedAt{};
		Time::TimePoint finishedAt{};
	};

	class JobContext;
	class IJob;

	struct DelayedSubmission
	{
		JobId id = 0;
		Ref<IJob> job;
		JobPriority priority = JobPriority::Normal;
		Time::SteadyTimePoint dueAt{};
	};

	struct JobRecord
	{
		JobSnapshot snapshot;
		std::vector<JobLogEntry> logs;
		Ref<std::atomic_bool> cancellationRequested = CreateRef<std::atomic_bool>(false);
		Ref<IJob> job;
	};

	class JobCancelledException final : public std::runtime_error
	{
	public:
		explicit JobCancelledException(const std::string &message = "Job cancelled")
			: std::runtime_error(message)
		{
		}
	};

	class IJob
	{
	public:
		virtual ~IJob() = default;

		[[nodiscard]] virtual std::string GetName() const = 0;
		[[nodiscard]] virtual std::string GetType() const = 0;
		virtual void Execute(JobContext &context) = 0;
	};
} // namespace DefectStudio
