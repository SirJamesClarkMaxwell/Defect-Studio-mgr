#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
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
		[[nodiscard]] JobId SubmitAfter(const Ref<IJob> &job, Time::Milliseconds delay, JobPriority priority = JobPriority::Normal);
		[[nodiscard]] bool RequestCancel(JobId id);
		[[nodiscard]] bool Resume(JobId id);
		[[nodiscard]] bool Reset(JobId id);
		[[nodiscard]] bool Retry(JobId id, JobPriority priority = JobPriority::Normal);
		[[nodiscard]] bool RemoveFromHistory(JobId id);
		[[nodiscard]] std::size_t GetThreadCount() const;
		[[nodiscard]] bool SetThreadCount(std::size_t threadCount);

		[[nodiscard]] std::optional<JobSnapshot> GetJob(JobId id) const;
		[[nodiscard]] std::vector<JobSnapshot> GetAllJobs() const;
		[[nodiscard]] std::vector<JobSnapshot> GetActiveJobs() const;
		[[nodiscard]] std::vector<JobSnapshot> GetFinishedJobs() const;
		[[nodiscard]] std::vector<JobLogEntry> GetLogs(JobId id) const;

		void Shutdown();

	private:
		// Thread pool helpers.
		static std::size_t resolveThreadCount(std::size_t requested);
		static BS::priority_t toBackendPriority(JobPriority priority);
		static bool isFinishedStatus(JobStatus status);

		// Submission and execution pipeline.
		[[nodiscard]] JobId submitInternal(const Ref<IJob> &job, JobPriority priority, JobId parentId, std::optional<Time::Milliseconds> delay);
		void enqueueForExecution(JobId id, const Ref<IJob> &job, JobPriority priority);
		void recordQueued(JobId id, const Ref<IJob> &job, const Time::TimePoint &now, JobId parentId, JobPriority priority);
		void runJob(JobId id, Ref<IJob> job);
		[[nodiscard]] bool waitForJobCooperative(JobId id);

		// Runtime thread-count reconfiguration worker.
		void threadCountWorkerLoop(std::stop_token stopToken);

		// Job state mutation helpers (records/logs/progress).
		void markRunning(JobId id, const Time::TimePoint &startedAt);
		void updateProgress(JobId id, float completedWork, float totalWork);
		void updateStage(JobId id, const std::string &stage);
		void updateMessage(JobId id, const std::string &message);
		void appendLog(JobId id, JobLogLevel level, const std::string &message);
		[[nodiscard]] bool isCancellationRequested(JobId id) const;
		void markFinished(JobId id, JobStatus finalStatus, const std::string &errorMessage, const Time::TimePoint &finishedAt);

		// Event publishing boundary.
		[[nodiscard]] std::optional<Ref<EventBus>> lockEventBus() const;
		void publishQueuedEvent(JobId id, const IJob &job, const Time::TimePoint &createdAt, JobId parentId, JobPriority priority) const;
		void publishStartedEvent(JobId id, const IJob &job, const Time::TimePoint &startedAt) const;
		void publishProgressEvent(JobId id, float completedWork, float totalWork, const std::string &stage, const std::string &message) const;
		void publishFinishedEvent(JobId id, const IJob &job, JobStatus status, const std::string &errorMessage, const Time::TimePoint &finishedAt) const;

		// Delayed submission worker.
		void delayedWorkerLoop(std::stop_token stopToken);
		void cancelPendingDelayedSubmissions();
		
	private:
		// Shared external integration.
		WeakRef<EventBus> m_EventBus;

		// Execution backend.
		BS::thread_pool<BS::tp::priority | BS::tp::pause> m_Pool;

		// Runtime thread-count reconfiguration channel.
		mutable std::mutex m_ThreadCountMutex;
		std::condition_variable m_ThreadCountCv;
		std::optional<std::size_t> m_PendingThreadCount;
		std::jthread m_ThreadCountWorker;

		// Job record storage.
		mutable std::mutex m_RecordsMutex;
		std::unordered_map<JobId, JobRecord> m_Records;

		// Delayed submission storage and dispatcher.
		mutable std::mutex m_DelayedMutex;
		std::condition_variable m_DelayedCv;
		std::deque<DelayedSubmission> m_DelayedSubmissions;
		std::jthread m_DelayedWorker;

		// Lifecycle control.
		std::atomic<JobId> m_NextId{1};
		std::atomic_bool m_ShutdownRequested{false};
		std::once_flag m_ShutdownOnce;
	};
} // namespace DefectStudio
