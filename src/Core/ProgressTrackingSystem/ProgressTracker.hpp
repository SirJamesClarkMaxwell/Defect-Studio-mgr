#pragma once

#include <concepts>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/EventSystem/BusEventSystem/SubscriptionHandle.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
	struct JobQueuedEvent;
	struct JobStartedEvent;
	struct JobProgressEvent;
	struct JobCompletedEvent;
	struct JobCancelledEvent;
	struct JobFailedEvent;


	template <typename EventType>
	concept TrackerEvent =
		std::same_as<EventType, JobQueuedEvent>
		|| std::same_as<EventType, JobStartedEvent>
		|| std::same_as<EventType, JobProgressEvent>
		|| std::same_as<EventType, JobCompletedEvent>
		|| std::same_as<EventType, JobCancelledEvent>
		|| std::same_as<EventType, JobFailedEvent>;


	struct ProgressEntrySnapshot
	{
		JobId id = 0;
		JobId parentId = 0;
		std::string source;
		std::string label;
		JobStatus status = JobStatus::Queued;
		JobPriority priority = JobPriority::Normal;
		float totalWork = 0.0f;
		float completedWork = 0.0f;
		std::string currentStage;
		std::string currentMessage;
		Time::TimePoint createdAt{};
		Time::TimePoint startedAt{};
		Time::TimePoint finishedAt{};
		std::string errorSummary;
		bool finished = false;

		// Natural ordering in tracker snapshots: by stable JobId.
		[[nodiscard]] bool operator<(const ProgressEntrySnapshot &other) const noexcept
		{
			return id < other.id;
		}
	};

	class ProgressTracker
	{
	public:
		explicit ProgressTracker(WeakRef<EventBus> eventBus = {});
		~ProgressTracker();

		ProgressTracker(const ProgressTracker &) = delete;
		ProgressTracker &operator=(const ProgressTracker &) = delete;
		ProgressTracker(ProgressTracker &&) = delete;
		ProgressTracker &operator=(ProgressTracker &&) = delete;

		void BindEventBus(WeakRef<EventBus> eventBus);
		void UnbindEventBus();

		[[nodiscard]] std::optional<ProgressEntrySnapshot> GetSnapshot(JobId id) const;
		[[nodiscard]] std::vector<ProgressEntrySnapshot> GetAllSnapshots() const;
		[[nodiscard]] std::vector<ProgressEntrySnapshot> GetActiveSnapshots() const;
		[[nodiscard]] std::vector<ProgressEntrySnapshot> GetFinishedSnapshots() const;
		[[nodiscard]] bool RemoveEntry(JobId id);

	private:
		void onQueued(const JobQueuedEvent &event);
		void onStarted(const JobStartedEvent &event);
		void onProgress(const JobProgressEvent &event);
		void onCompleted(const JobCompletedEvent &event);
		void onCancelled(const JobCancelledEvent &event);
		void onFailed(const JobFailedEvent &event);

	private:
		mutable std::mutex m_Mutex;
		std::unordered_map<JobId, ProgressEntrySnapshot> m_Entries;
		WeakRef<EventBus> m_EventBus;
		SubscriptionHandle m_QueuedSubscription;
		SubscriptionHandle m_StartedSubscription;
		SubscriptionHandle m_ProgressSubscription;
		SubscriptionHandle m_CompletedSubscription;
		SubscriptionHandle m_CancelledSubscription;
		SubscriptionHandle m_FailedSubscription;
	};
} // namespace DefectStudio