#include "Core/dspch.hpp"

#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

#include <algorithm>
#include <functional>

#include "App/Events/JobEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"

namespace DefectStudio
{
	template <TrackerEvent EventType>
	SubscriptionHandle subscribeTrackerMember(
		EventBus &bus,
		ProgressTracker &tracker,
		void (ProgressTracker::*method)(const EventType &))
	{
		return bus.Subscribe<EventType>(std::bind_front(method, &tracker));
	}

	ProgressTracker::ProgressTracker(WeakRef<EventBus> eventBus)
	{
		BindEventBus(std::move(eventBus));
	}

	ProgressTracker::~ProgressTracker()
	{
		UnbindEventBus();
	}

	void ProgressTracker::BindEventBus(WeakRef<EventBus> eventBus)
	{
		UnbindEventBus();
		m_EventBus = std::move(eventBus);

		auto bus = m_EventBus.lock();
		if (!bus)
			return;

		m_QueuedSubscription = subscribeTrackerMember<JobQueuedEvent>(*bus, *this, &ProgressTracker::onQueued);
		m_StartedSubscription = subscribeTrackerMember<JobStartedEvent>(*bus, *this, &ProgressTracker::onStarted);
		m_ProgressSubscription = subscribeTrackerMember<JobProgressEvent>(*bus, *this, &ProgressTracker::onProgress);
		m_CompletedSubscription = subscribeTrackerMember<JobCompletedEvent>(*bus, *this, &ProgressTracker::onCompleted);
		m_CancelledSubscription = subscribeTrackerMember<JobCancelledEvent>(*bus, *this, &ProgressTracker::onCancelled);
		m_FailedSubscription = subscribeTrackerMember<JobFailedEvent>(*bus, *this, &ProgressTracker::onFailed);
		
	}

	void ProgressTracker::UnbindEventBus()
	{
		m_QueuedSubscription.Reset();
		m_StartedSubscription.Reset();
		m_ProgressSubscription.Reset();
		m_CompletedSubscription.Reset();
		m_CancelledSubscription.Reset();
		m_FailedSubscription.Reset();
		m_EventBus.reset();
	}

	std::optional<ProgressEntrySnapshot> ProgressTracker::GetSnapshot(JobId id) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Entries.find(id);
		if (it == m_Entries.end())
			return std::nullopt;

		return it->second;
	}

	std::vector<ProgressEntrySnapshot> ProgressTracker::GetAllSnapshots() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		std::vector<ProgressEntrySnapshot> entries;
		entries.reserve(m_Entries.size());
		for (const auto &[id, entry] : m_Entries)
		{
			(void)id;
			entries.push_back(entry);
		}

		std::sort(entries.begin(), entries.end());
		return entries;
	}

	std::vector<ProgressEntrySnapshot> ProgressTracker::GetActiveSnapshots() const
	{
		auto entries = GetAllSnapshots();
		entries.erase(std::remove_if(entries.begin(), entries.end(), [](const ProgressEntrySnapshot &entry) {
			return entry.finished;
		}), entries.end());
		return entries;
	}

	std::vector<ProgressEntrySnapshot> ProgressTracker::GetFinishedSnapshots() const
	{
		auto entries = GetAllSnapshots();
		entries.erase(std::remove_if(entries.begin(), entries.end(), [](const ProgressEntrySnapshot &entry) {
			return !entry.finished;
		}), entries.end());
		return entries;
	}

	bool ProgressTracker::RemoveEntry(JobId id)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Entries.erase(id) > 0;
	}

	void ProgressTracker::onQueued(const JobQueuedEvent &event)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto &entry = m_Entries[event.id];
		entry.id = event.id;
		entry.parentId = event.parentId;
		entry.source = event.type;
		entry.label = event.name;
		entry.status = JobStatus::Queued;
		entry.priority = event.priority;
		entry.createdAt = event.createdAt;
		entry.finished = false;
		entry.errorSummary.clear();
	}

	void ProgressTracker::onStarted(const JobStartedEvent &event)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto &entry = m_Entries[event.id];
		entry.id = event.id;
		entry.status = JobStatus::Running;
		entry.startedAt = event.startedAt;
		entry.finished = false;
	}

	void ProgressTracker::onProgress(const JobProgressEvent &event)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto &entry = m_Entries[event.id];
		entry.id = event.id;
		if (entry.status == JobStatus::Queued)
			entry.status = JobStatus::Running;

		entry.completedWork = event.completedWork;
		entry.totalWork = event.totalWork;
		entry.currentStage = event.stage;
		entry.currentMessage = event.message;
	}

	void ProgressTracker::onCompleted(const JobCompletedEvent &event)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto &entry = m_Entries[event.id];
		entry.id = event.id;
		entry.status = JobStatus::Completed;
		entry.finishedAt = event.finishedAt;
		entry.finished = true;
		if (entry.totalWork > 0.0f && entry.completedWork < entry.totalWork)
			entry.completedWork = entry.totalWork;
	}

	void ProgressTracker::onCancelled(const JobCancelledEvent &event)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto &entry = m_Entries[event.id];
		entry.id = event.id;
		entry.status = JobStatus::Cancelled;
		entry.finishedAt = event.finishedAt;
		entry.finished = true;
	}

	void ProgressTracker::onFailed(const JobFailedEvent &event)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto &entry = m_Entries[event.id];
		entry.id = event.id;
		entry.status = JobStatus::Failed;
		entry.errorSummary = event.errorMessage;
		entry.finishedAt = event.finishedAt;
		entry.finished = true;
	}
} // namespace DefectStudio