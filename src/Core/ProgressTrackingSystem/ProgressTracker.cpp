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

	ProgressTracker::ProgressTracker(Ref<EventBus> eventBus)
	{
		BindEventBus(std::move(eventBus));
	}

	ProgressTracker::~ProgressTracker()
	{
		UnbindEventBus();
	}

	void ProgressTracker::BindEventBus(Ref<EventBus> eventBus)
	{
		ZoneScoped;
		UnbindEventBus();
		m_EventBus = std::move(eventBus);

		if (!m_EventBus)
			return;

		m_QueuedSubscription = subscribeTrackerMember<JobQueuedEvent>(*m_EventBus, *this, &ProgressTracker::onQueued);
		m_StartedSubscription = subscribeTrackerMember<JobStartedEvent>(*m_EventBus, *this, &ProgressTracker::onStarted);
		m_ProgressSubscription = subscribeTrackerMember<JobProgressEvent>(*m_EventBus, *this, &ProgressTracker::onProgress);
		m_CompletedSubscription = subscribeTrackerMember<JobCompletedEvent>(*m_EventBus, *this, &ProgressTracker::onCompleted);
		m_CancelledSubscription = subscribeTrackerMember<JobCancelledEvent>(*m_EventBus, *this, &ProgressTracker::onCancelled);
		m_FailedSubscription = subscribeTrackerMember<JobFailedEvent>(*m_EventBus, *this, &ProgressTracker::onFailed);
		
	}

	void ProgressTracker::UnbindEventBus()
	{
		ZoneScoped;
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
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Entries.find(id);
		if (it == m_Entries.end())
			return std::nullopt;

		return it->second;
	}

	std::vector<ProgressEntrySnapshot> ProgressTracker::GetAllSnapshots() const
	{
		ZoneScoped;
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
		ZoneScoped;
		auto entries = GetAllSnapshots();
		entries.erase(std::remove_if(entries.begin(), entries.end(), [](const ProgressEntrySnapshot &entry) {
			return entry.finished;
		}), entries.end());
		return entries;
	}

	std::vector<ProgressEntrySnapshot> ProgressTracker::GetFinishedSnapshots() const
	{
		ZoneScoped;
		auto entries = GetAllSnapshots();
		entries.erase(std::remove_if(entries.begin(), entries.end(), [](const ProgressEntrySnapshot &entry) {
			return !entry.finished;
		}), entries.end());
		return entries;
	}

	bool ProgressTracker::RemoveEntry(JobId id)
	{
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Entries.erase(id) > 0;
	}

	void ProgressTracker::onQueued(const JobQueuedEvent &event)
	{
		ZoneScoped;
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
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto &entry = m_Entries[event.id];
		entry.id = event.id;
		entry.status = JobStatus::Running;
		entry.startedAt = event.startedAt;
		entry.finished = false;
	}

	void ProgressTracker::onProgress(const JobProgressEvent &event)
	{
		ZoneScoped;
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
		ZoneScoped;
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
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto &entry = m_Entries[event.id];
		entry.id = event.id;
		entry.status = JobStatus::Cancelled;
		entry.finishedAt = event.finishedAt;
		entry.finished = true;
	}

	void ProgressTracker::onFailed(const JobFailedEvent &event)
	{
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto &entry = m_Entries[event.id];
		entry.id = event.id;
		entry.status = JobStatus::Failed;
		entry.errorSummary = event.errorMessage;
		entry.finishedAt = event.finishedAt;
		entry.finished = true;
	}
} // namespace DefectStudio
