#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <BS_thread_pool.hpp>

#include "Core/Utils/Memory.hpp"

#include "Core/JobSystem/JobSystemTypes.hpp"

namespace DefectStudio
{
	class EventBus;

	class JobSystem
	{
	public:
		explicit JobSystem(WeakRef<EventBus> eventBus = {}, std::size_t threadCount = 0);
		~JobSystem();

		JobSystem(const JobSystem &) = delete;
		JobSystem &operator=(const JobSystem &) = delete;
		JobSystem(JobSystem &&) = delete;
		JobSystem &operator=(JobSystem &&) = delete;

		[[nodiscard]] JobId Submit(const Ref<IJob> &job, JobPriority priority = JobPriority::Normal);
		[[nodiscard]] bool RequestCancel(JobId id);

		[[nodiscard]] std::optional<JobSnapshot> GetJob(JobId id) const;
		[[nodiscard]] std::vector<JobSnapshot> GetAllJobs() const;
		[[nodiscard]] std::vector<JobSnapshot> GetActiveJobs() const;
		[[nodiscard]] std::vector<JobSnapshot> GetFinishedJobs() const;
		[[nodiscard]] std::vector<JobLogEntry> GetLogs(JobId id) const;

		void Shutdown();

	private:
		static std::size_t resolveThreadCount(std::size_t requested);
		static BS::priority_t toBackendPriority(JobPriority priority);
		static bool isFinishedStatus(JobStatus status);

		void recordQueued(JobId id, const Ref<IJob> &job, const Time::TimePoint &now);
		void runJob(JobId id, Ref<IJob> job);
		void markRunning(JobId id, const Time::TimePoint &startedAt);
		void updateProgress(JobId id, float completedWork, float totalWork);
		void updateStage(JobId id, const std::string &stage);
		void updateMessage(JobId id, const std::string &message);
		void appendLog(JobId id, JobLogLevel level, const std::string &message);
		[[nodiscard]] bool isCancellationRequested(JobId id) const;
		void markFinished(JobId id, JobStatus finalStatus, const std::string &errorMessage, const Time::TimePoint &finishedAt);
		[[nodiscard]] std::optional<Ref<EventBus>> lockEventBus() const;
		void publishStartedEvent(JobId id, const IJob &job) const;
		void publishFinishedEvent(JobId id, const IJob &job, JobStatus status, const std::string &errorMessage) const;

		WeakRef<EventBus> m_EventBus;
		BS::priority_thread_pool m_Pool;
		mutable std::mutex m_Mutex;
		std::unordered_map<JobId, JobRecord> m_Records;
		std::atomic<JobId> m_NextId{1};
		std::atomic_bool m_ShutdownRequested{false};
		std::once_flag m_ShutdownOnce;
	};
} // namespace DefectStudio
