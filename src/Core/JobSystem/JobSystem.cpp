#include "Core/dspch.hpp"

#include "Core/JobSystem/JobSystem.hpp"


#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Time.hpp"
#include "App/Events/JobEvents.hpp"

namespace DefectStudio
{
	std::size_t JobSystem::resolveThreadCount(std::size_t requested)
	{
		if (requested != 0)
			return requested;

		const std::size_t detected = std::thread::hardware_concurrency(); 
		return detected == 0 ? 1 : detected;
	}

	BS::priority_t JobSystem::toBackendPriority(JobPriority priority)
	{
		switch (priority)
		{
		case JobPriority::Highest:
			return BS::pr::highest;
		case JobPriority::High:
			return BS::pr::high;
		case JobPriority::Normal:
			return BS::pr::normal;
		case JobPriority::Low:
			return BS::pr::low;
		case JobPriority::Lowest:
		default:
			return BS::pr::lowest;
		}
	}

	bool JobSystem::isFinishedStatus(JobStatus status)
	{
		return status == JobStatus::Completed || status == JobStatus::Failed || status == JobStatus::Cancelled;
	}

	JobSystem::JobSystem(WeakRef<EventBus> eventBus, std::size_t threadCount)
		: m_EventBus(std::move(eventBus)),
		  m_Pool(resolveThreadCount(threadCount)) // todo: load it from config file
	{
		m_DelayedWorker = std::jthread([this](std::stop_token stopToken) {
			delayedWorkerLoop(stopToken);
		});
	}

	JobSystem::~JobSystem()
	{
		Shutdown();
	}

	JobId JobSystem::Submit(const Ref<IJob> &job, JobPriority priority)
	{
		if (!job)
			return 0;

		if (m_ShutdownRequested.load(std::memory_order_relaxed))
			return 0;

		const JobId id = m_NextId.fetch_add(1, std::memory_order_relaxed);
		const auto now = Time::Now();
		recordQueued(id, job, now);
		publishQueuedEvent(id, *job, now);
		enqueueForExecution(id, job, priority);

		return id;
	}

	JobId JobSystem::SubmitAfter(const Ref<IJob> &job, Time::Milliseconds delay, JobPriority priority)
	{
		if (!job)
			return 0;

		if (m_ShutdownRequested.load(std::memory_order_relaxed))
			return 0;

		if (delay.count() <= 0)
			return Submit(job, priority);

		const JobId id = m_NextId.fetch_add(1, std::memory_order_relaxed);
		const auto now = Time::Now();
		recordQueued(id, job, now);
		publishQueuedEvent(id, *job, now);

		{
			std::lock_guard<std::mutex> lock(m_DelayedMutex);
			m_DelayedSubmissions.push_back(DelayedSubmission{
				.id = id,
				.job = job,
				.priority = priority,
				.dueAt = Time::NowSteady() + delay,
			});
		}

		m_DelayedCv.notify_all();
		return id;
	}

	bool JobSystem::RequestCancel(JobId id)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return false;

		it->second.cancellationRequested->store(true, std::memory_order_relaxed);
		it->second.snapshot.cancellationRequested = true;
		return true;
	}

	bool JobSystem::Reset(JobId id)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return false;

		// Only allow reset on finished jobs
		if (!isFinishedStatus(it->second.snapshot.status))
			return false;

		// Reset to Queued state
		it->second.snapshot.status = JobStatus::Queued;
		it->second.snapshot.startedAt = Time::TimePoint{};
		it->second.snapshot.finishedAt = Time::TimePoint{};
		it->second.snapshot.errorMessage.clear();
		it->second.snapshot.cancellationRequested = false;
		it->second.snapshot.completedWork = 0.0f;
		it->second.snapshot.currentStage.clear();
		it->second.snapshot.currentMessage.clear();
		it->second.cancellationRequested->store(false, std::memory_order_relaxed);
		it->second.logs.push_back(JobLogEntry{JobLogLevel::Info, "Reset", Time::Now()});
		return true;
	}

	bool JobSystem::Retry(JobId id, JobPriority priority)
	{
		Ref<IJob> jobToRetry;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			auto it = m_Records.find(id);
			if (it == m_Records.end())
				return false;

			// Only allow retry on finished jobs
			if (!isFinishedStatus(it->second.snapshot.status))
				return false;

			jobToRetry = it->second.job;
			if (!jobToRetry)
				return false;

			// Reset to Queued state
			it->second.snapshot.status = JobStatus::Queued;
			it->second.snapshot.startedAt = Time::TimePoint{};
			it->second.snapshot.finishedAt = Time::TimePoint{};
			it->second.snapshot.errorMessage.clear();
			it->second.snapshot.cancellationRequested = false;
			it->second.snapshot.completedWork = 0.0f;
			it->second.snapshot.currentStage.clear();
			it->second.snapshot.currentMessage.clear();
			it->second.cancellationRequested->store(false, std::memory_order_relaxed);
			it->second.logs.push_back(JobLogEntry{JobLogLevel::Info, "Retry", Time::Now()});
		}

		// Re-enqueue without holding mutex
		enqueueForExecution(id, jobToRetry, priority);
		return true;
	}

	std::size_t JobSystem::GetThreadCount() const
	{
		return m_Pool.get_thread_count();
	}

	bool JobSystem::SetThreadCount(std::size_t threadCount)
	{
		if (m_ShutdownRequested.load(std::memory_order_relaxed))
			return false;

		const std::size_t resolved = resolveThreadCount(threadCount);
		if (resolved == m_Pool.get_thread_count())
			return true;

		m_Pool.reset(resolved);
		return true;
	}

	std::optional<JobSnapshot> JobSystem::GetJob(JobId id) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return std::nullopt;

		return it->second.snapshot;
	}

	std::vector<JobSnapshot> JobSystem::GetAllJobs() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		std::vector<JobSnapshot> jobs;
		jobs.reserve(m_Records.size());
		for (const auto &entry : m_Records)
			jobs.push_back(entry.second.snapshot);

		std::sort(jobs.begin(), jobs.end(), [](const JobSnapshot &left, const JobSnapshot &right) {
			return left.id < right.id;
		});
		return jobs;
	}

	std::vector<JobSnapshot> JobSystem::GetActiveJobs() const
	{
		auto jobs = GetAllJobs();
		jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const JobSnapshot &snapshot) {
			return isFinishedStatus(snapshot.status);
		}), jobs.end());
		return jobs;
	}

	std::vector<JobSnapshot> JobSystem::GetFinishedJobs() const
	{
		auto jobs = GetAllJobs();
		jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const JobSnapshot &snapshot) {
			return !isFinishedStatus(snapshot.status);
		}), jobs.end());
		return jobs;
	}

	std::vector<JobLogEntry> JobSystem::GetLogs(JobId id) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return {};

		return it->second.logs;
	}

	void JobSystem::Shutdown()
	{
		std::call_once(m_ShutdownOnce, [this]() {
			m_ShutdownRequested.store(true, std::memory_order_relaxed);
			m_DelayedWorker.request_stop();
			m_DelayedCv.notify_all();
			if (m_DelayedWorker.joinable())
				m_DelayedWorker.join();

			cancelPendingDelayedSubmissions();
			m_Pool.wait();
		});
	}

	void JobSystem::enqueueForExecution(JobId id, const Ref<IJob> &job, JobPriority priority)
	{
		if (!job)
			return;

		m_Pool.detach_task(
			[this, id, job]() {
				runJob(id, job);
			},
			toBackendPriority(priority));
	}

	void JobSystem::recordQueued(JobId id, const Ref<IJob> &job, const Time::TimePoint &now)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		JobRecord record;
		record.snapshot.id = id;
		record.snapshot.name = job->GetName();
		record.snapshot.type = job->GetType();
		record.snapshot.status = JobStatus::Queued;
		record.snapshot.createdAt = now;
		record.job = job;
		record.logs.push_back(JobLogEntry{JobLogLevel::Info, "Queued", now});
		m_Records.emplace(id, std::move(record));
	}

	void JobSystem::runJob(JobId id, Ref<IJob> job)
	{
		const auto startedAt = Time::Now();
		markRunning(id, startedAt);
		publishStartedEvent(id, *job, startedAt);

		JobContext context(
			[this, id](float completedWork, float totalWork) { updateProgress(id, completedWork, totalWork); },
			[this, id](const std::string &stage) { updateStage(id, stage); },
			[this, id](const std::string &message) { updateMessage(id, message); },
			[this, id](JobLogLevel level, const std::string &message) { appendLog(id, level, message); },
			[this, id]() { return isCancellationRequested(id); },
			[this](const Ref<IJob> &job, JobPriority priority) { return Submit(job, priority); },
			[this](JobId childId) {
				for (;;)
				{
					if (m_ShutdownRequested.load(std::memory_order_relaxed))
						return false;

					auto child = GetJob(childId);
					if (child.has_value() && isFinishedStatus(child->status))
						return true;

					std::this_thread::sleep_for(Time::Milliseconds(1));
				}
			});

		JobStatus finalStatus = JobStatus::Completed;
		std::string errorMessage;

		try
		{
			context.ThrowIfCancellationRequested();
			job->Execute(context);
			if (context.IsCancellationRequested())
				finalStatus = JobStatus::Cancelled;
		}
		catch (const JobCancelledException &ex)
		{
			finalStatus = JobStatus::Cancelled;
			errorMessage = ex.what();
		}
		catch (const std::exception &ex)
		{
			finalStatus = JobStatus::Failed;
			errorMessage = ex.what();
		}
		catch (...)
		{
			finalStatus = JobStatus::Failed;
			errorMessage = "Unknown error";
		}

		const auto finishedAt = Time::Now();
		markFinished(id, finalStatus, errorMessage, finishedAt);
		publishFinishedEvent(id, *job, finalStatus, errorMessage, finishedAt);
	}

	void JobSystem::markRunning(JobId id, const Time::TimePoint &startedAt)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return;

		it->second.snapshot.status = JobStatus::Running;
		it->second.snapshot.startedAt = startedAt;
		it->second.logs.push_back(JobLogEntry{JobLogLevel::Info, "Running", startedAt});
	}

	void JobSystem::updateProgress(JobId id, float completedWork, float totalWork)
	{
		std::string stage;
		std::string message;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			auto it = m_Records.find(id);
			if (it == m_Records.end())
				return;

			it->second.snapshot.completedWork = completedWork;
			it->second.snapshot.totalWork = totalWork;
			stage = it->second.snapshot.currentStage;
			message = it->second.snapshot.currentMessage;
		}

		publishProgressEvent(id, completedWork, totalWork, stage, message);
	}

	void JobSystem::updateStage(JobId id, const std::string &stage)
	{
		float completedWork = 0.0f;
		float totalWork = 0.0f;
		std::string message;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			auto it = m_Records.find(id);
			if (it == m_Records.end())
				return;

			it->second.snapshot.currentStage = stage;
			completedWork = it->second.snapshot.completedWork;
			totalWork = it->second.snapshot.totalWork;
			message = it->second.snapshot.currentMessage;
		}

		publishProgressEvent(id, completedWork, totalWork, stage, message);
	}

	void JobSystem::updateMessage(JobId id, const std::string &message)
	{
		float completedWork = 0.0f;
		float totalWork = 0.0f;
		std::string stage;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			auto it = m_Records.find(id);
			if (it == m_Records.end())
				return;

			it->second.snapshot.currentMessage = message;
			completedWork = it->second.snapshot.completedWork;
			totalWork = it->second.snapshot.totalWork;
			stage = it->second.snapshot.currentStage;
		}

		publishProgressEvent(id, completedWork, totalWork, stage, message);
	}

	void JobSystem::appendLog(JobId id, JobLogLevel level, const std::string &message)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return;

		it->second.logs.push_back(JobLogEntry{level, message, Time::Now()});
	}

	bool JobSystem::isCancellationRequested(JobId id) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return true;

		return it->second.cancellationRequested->load(std::memory_order_relaxed);
	}

	void JobSystem::markFinished(JobId id, JobStatus finalStatus, const std::string &errorMessage, const Time::TimePoint &finishedAt)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return;

		it->second.snapshot.status = finalStatus;
		it->second.snapshot.finishedAt = finishedAt;
		it->second.snapshot.cancellationRequested = it->second.cancellationRequested->load(std::memory_order_relaxed);

		if (finalStatus == JobStatus::Completed)
		{
			if (it->second.snapshot.totalWork > 0.0f && it->second.snapshot.completedWork < it->second.snapshot.totalWork)
				it->second.snapshot.completedWork = it->second.snapshot.totalWork;

			it->second.logs.push_back(JobLogEntry{JobLogLevel::Info, "Completed", finishedAt});
		}
		else if (finalStatus == JobStatus::Cancelled)
		{
			if (!errorMessage.empty())
				it->second.snapshot.errorMessage = errorMessage;

			it->second.logs.push_back(JobLogEntry{JobLogLevel::Warning, "Cancelled", finishedAt});
		}
		else
		{
			it->second.snapshot.errorMessage = errorMessage;
			it->second.logs.push_back(JobLogEntry{JobLogLevel::Error, errorMessage, finishedAt});
		}
	}

	std::optional<Ref<EventBus>> JobSystem::lockEventBus() const
	{
		auto eventBus = m_EventBus.lock();
		if (!eventBus)
			return std::nullopt;

		return eventBus;
	}

	void JobSystem::publishQueuedEvent(JobId id, const IJob &job, const Time::TimePoint &createdAt) const
	{
		auto eventBus = lockEventBus();
		if (!eventBus)
			return;

		(*eventBus)->Queue(JobQueuedEvent{id, job.GetName(), job.GetType(), createdAt});
	}

	void JobSystem::publishStartedEvent(JobId id, const IJob &job, const Time::TimePoint &startedAt) const
	{
		auto eventBus = lockEventBus();
		if (!eventBus)
			return;

		(*eventBus)->Queue(JobStartedEvent{id, job.GetName(), job.GetType(), startedAt});
	}

	void JobSystem::publishProgressEvent(JobId id, float completedWork, float totalWork, const std::string &stage, const std::string &message) const
	{
		auto eventBus = lockEventBus();
		if (!eventBus)
			return;

		(*eventBus)->Queue(JobProgressEvent{id, completedWork, totalWork, stage, message, Time::Now()});
	}

	void JobSystem::publishFinishedEvent(
		JobId id,
		const IJob &job,
		JobStatus status,
		const std::string &errorMessage,
		const Time::TimePoint &finishedAt) const
	{
		auto eventBus = lockEventBus();
		if (!eventBus)
			return;

		if (status == JobStatus::Completed)
			(*eventBus)->Queue(JobCompletedEvent{id, job.GetName(), job.GetType(), finishedAt});
		else if (status == JobStatus::Cancelled)
			(*eventBus)->Queue(JobCancelledEvent{id, job.GetName(), job.GetType(), finishedAt});
		else
			(*eventBus)->Queue(JobFailedEvent{id, job.GetName(), job.GetType(), errorMessage, finishedAt});
	}

	void JobSystem::delayedWorkerLoop(std::stop_token stopToken)
	{
		for (;;)
		{
			if (stopToken.stop_requested() || m_ShutdownRequested.load(std::memory_order_relaxed))
				return;

			DelayedSubmission submission;
			bool hasReadySubmission = false;

			{
				std::unique_lock<std::mutex> lock(m_DelayedMutex);
				m_DelayedCv.wait(lock, [this, &stopToken]() {
					return stopToken.stop_requested()
						|| m_ShutdownRequested.load(std::memory_order_relaxed)
						|| !m_DelayedSubmissions.empty();
				});

				if (stopToken.stop_requested() || m_ShutdownRequested.load(std::memory_order_relaxed))
					return;

				auto nextIt = std::min_element(
					m_DelayedSubmissions.begin(),
					m_DelayedSubmissions.end(),
					[](const DelayedSubmission &left, const DelayedSubmission &right) {
						return left.dueAt < right.dueAt;
					});

				if (nextIt == m_DelayedSubmissions.end())
					continue;

				const auto now = Time::NowSteady();
				if (nextIt->dueAt > now)
				{
					m_DelayedCv.wait_until(lock, nextIt->dueAt, [this, &stopToken]() {
						return stopToken.stop_requested()
							|| m_ShutdownRequested.load(std::memory_order_relaxed);
					});
					continue;
				}

				submission = std::move(*nextIt);
				m_DelayedSubmissions.erase(nextIt);
				hasReadySubmission = true;
			}

			if (hasReadySubmission && !m_ShutdownRequested.load(std::memory_order_relaxed))
				enqueueForExecution(submission.id, submission.job, submission.priority);
		}
	}

	void JobSystem::cancelPendingDelayedSubmissions()
	{
		std::deque<DelayedSubmission> pending;
		{
			std::lock_guard<std::mutex> lock(m_DelayedMutex);
			pending.swap(m_DelayedSubmissions);
		}

		const auto finishedAt = Time::Now();
		for (const auto &submission : pending)
			markFinished(submission.id, JobStatus::Cancelled, "Cancelled by shutdown before dispatch", finishedAt);
	}
} // namespace DefectStudio
