#include "Core/dspch.hpp"

#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

#include <algorithm>

#include "App/Events/JobEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"

namespace DefectStudio
{
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

		m_QueuedSubscription = bus->Subscribe<JobQueuedEvent>([this](const JobQueuedEvent &event) {
			onQueued(event);
		});

		m_StartedSubscription = bus->Subscribe<JobStartedEvent>([this](const JobStartedEvent &event) {
			onStarted(event);
		});

		m_ProgressSubscription = bus->Subscribe<JobProgressEvent>([this](const JobProgressEvent &event) {
			onProgress(event);
		});

		m_CompletedSubscription = bus->Subscribe<JobCompletedEvent>([this](const JobCompletedEvent &event) {
			onCompleted(event);
		});

		m_CancelledSubscription = bus->Subscribe<JobCancelledEvent>([this](const JobCancelledEvent &event) {
			onCancelled(event);
		});

		m_FailedSubscription = bus->Subscribe<JobFailedEvent>([this](const JobFailedEvent &event) {
			onFailed(event);
		});
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

		std::sort(entries.begin(), entries.end(), [](const ProgressEntrySnapshot &left, const ProgressEntrySnapshot &right) {
			return left.id > right.id;
		});
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