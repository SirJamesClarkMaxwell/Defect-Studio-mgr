#include "Core/dspch.hpp"

#include "Core/JobSystem/JobSystem.hpp"


#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Time.hpp"
#include "App/Events/JobEvents.hpp"

namespace DefectStudio
{
	namespace
	{
		thread_local const JobSystem *g_CurrentJobSystem = nullptr;
		thread_local JobId g_CurrentJobId = 0;

		struct WorkerContextGuard
		{
			explicit WorkerContextGuard(const JobSystem *jobSystem, JobId jobId)
			{
				g_CurrentJobSystem = jobSystem;
				g_CurrentJobId = jobId;
			}

			~WorkerContextGuard()
			{
				g_CurrentJobSystem = nullptr;
				g_CurrentJobId = 0;
			}
		};
	}

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

	JobSystem::JobSystem(Ref<EventBus> eventBus, std::size_t threadCount)
		: m_EventBus(std::move(eventBus)),
		  m_Pool(resolveThreadCount(threadCount)) // todo: load it from config file
	{
		m_ThreadCountWorker = std::jthread([this](std::stop_token stopToken) {
			threadCountWorkerLoop(stopToken);
		});
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
		ZoneScoped;
		return submitInternal(job, priority, 0, std::nullopt);
	}

	JobId JobSystem::SubmitAfter(const Ref<IJob> &job, Time::Milliseconds delay, JobPriority priority)
	{
		ZoneScoped;
		return submitInternal(job, priority, 0, delay);
	}

	JobId JobSystem::submitInternal(const Ref<IJob> &job, JobPriority priority, JobId parentId, std::optional<Time::Milliseconds> delay)
	{
		ZoneScoped;
		if (!job)
			return 0;

		if (m_ShutdownRequested.load(std::memory_order_relaxed))
			return 0;

		const bool hasDelay = delay.has_value() && delay->count() > 0;
		if (delay.has_value() && delay->count() <= 0)
			return submitInternal(job, priority, parentId, std::nullopt);

		const JobId id = m_NextId.fetch_add(1, std::memory_order_relaxed);
		const auto now = Time::Now();
		recordQueued(id, job, now, parentId, priority);
		publishQueuedEvent(id, *job, now, parentId, priority);

		if (hasDelay)
		{
			std::lock_guard<std::mutex> lock(m_DelayedMutex);
			m_DelayedSubmissions.push_back(DelayedSubmission{
				.id = id,
				.job = job,
				.priority = priority,
				.dueAt = Time::NowSteady() + *delay,
			});
			m_DelayedCv.notify_all();
			return id;
		}

		enqueueForExecution(id, job, priority);
		return id;
	}

	bool JobSystem::RequestCancel(JobId id)
	{
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return false;

		it->second.cancellationRequested->store(true, std::memory_order_relaxed);
		it->second.snapshot.cancellationRequested = true;
		return true;
	}

	bool JobSystem::Resume(JobId id)
	{
		ZoneScoped;
		Ref<IJob> jobToResume;
		JobPriority priority = JobPriority::Normal;
		bool shouldEnqueue = false;

		{
			std::lock_guard<std::mutex> lock(m_RecordsMutex);
			auto it = m_Records.find(id);
			if (it == m_Records.end())
				return false;

			it->second.cancellationRequested->store(false, std::memory_order_relaxed);
			it->second.snapshot.cancellationRequested = false;

			if (it->second.snapshot.status == JobStatus::Cancelled)
			{
				jobToResume = it->second.job;
				if (!jobToResume)
					return false;

				priority = it->second.snapshot.priority;
				it->second.snapshot.status = JobStatus::Queued;
				it->second.snapshot.startedAt = Time::TimePoint{};
				it->second.snapshot.finishedAt = Time::TimePoint{};
				it->second.snapshot.errorMessage.clear();
				it->second.snapshot.completedWork = 0.0f;
				it->second.snapshot.currentStage.clear();
				it->second.snapshot.currentMessage.clear();
				it->second.logs.push_back(JobLogEntry{JobLogLevel::Info, "Resume", Time::Now()});
				shouldEnqueue = true;
			}
			else
			{
				it->second.logs.push_back(JobLogEntry{JobLogLevel::Info, "Resume request cleared cancellation", Time::Now()});
			}
		}

		if (shouldEnqueue)
			enqueueForExecution(id, jobToResume, priority);

		return true;
	}

	bool JobSystem::Reset(JobId id)
	{
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
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
		ZoneScoped;
		Ref<IJob> jobToRetry;
		{
			std::lock_guard<std::mutex> lock(m_RecordsMutex);
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

	bool JobSystem::RemoveFromHistory(JobId id)
	{
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return false;

		if (!isFinishedStatus(it->second.snapshot.status))
			return false;

		m_Records.erase(it);
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

		{
			std::lock_guard<std::mutex> lock(m_ThreadCountMutex);
			m_PendingThreadCount = resolved;
		}
		m_ThreadCountCv.notify_all();
		return true;
	}

	std::optional<JobSnapshot> JobSystem::GetJob(JobId id) const
	{
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return std::nullopt;

		return it->second.snapshot;
	}

	std::vector<JobSnapshot> JobSystem::GetAllJobs() const
	{
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		std::vector<JobSnapshot> jobs;
		jobs.reserve(m_Records.size());
		for (const auto &entry : m_Records)
			jobs.push_back(entry.second.snapshot);

		std::sort(jobs.begin(), jobs.end());
		return jobs;
	}

	std::vector<JobSnapshot> JobSystem::GetActiveJobs() const
	{
		ZoneScoped;
		auto jobs = GetAllJobs();
		jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const JobSnapshot &snapshot) {
			return isFinishedStatus(snapshot.status);
		}), jobs.end());
		return jobs;
	}

	std::vector<JobSnapshot> JobSystem::GetFinishedJobs() const
	{
		ZoneScoped;
		auto jobs = GetAllJobs();
		jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const JobSnapshot &snapshot) {
			return !isFinishedStatus(snapshot.status);
		}), jobs.end());
		return jobs;
	}

	std::vector<JobLogEntry> JobSystem::GetLogs(JobId id) const
	{
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return {};

		return it->second.logs;
	}

	void JobSystem::Shutdown()
	{
		std::call_once(m_ShutdownOnce, [this]() 
		{
			ZoneScoped;
			m_ShutdownRequested.store(true, std::memory_order_relaxed);
			m_ThreadCountCv.notify_all();

			if (m_ThreadCountWorker.joinable())
				m_ThreadCountWorker.request_stop();

			if (m_ThreadCountWorker.joinable())
				m_ThreadCountWorker.join();

			m_DelayedWorker.request_stop();
			m_DelayedCv.notify_all();

			if (m_DelayedWorker.joinable())
				m_DelayedWorker.join();

			cancelPendingDelayedSubmissions();
			m_Pool.wait();
		});
	}

	void JobSystem::threadCountWorkerLoop(std::stop_token stopToken)
	{
		ZoneScoped;
		while (true)
		{
			std::size_t requestedThreadCount = 0;
			{
				std::unique_lock<std::mutex> lock(m_ThreadCountMutex);
				m_ThreadCountCv.wait(lock, [&]() {
					return stopToken.stop_requested() || m_ShutdownRequested.load(std::memory_order_relaxed) || m_PendingThreadCount.has_value();
				});

				if (stopToken.stop_requested() || m_ShutdownRequested.load(std::memory_order_relaxed))
					return;

				requestedThreadCount = *m_PendingThreadCount;
				m_PendingThreadCount.reset();
			}

			if (m_ShutdownRequested.load(std::memory_order_relaxed))
				return;

			// reset() pauses the backend pool, waits only for currently running tasks,
			// applies the new thread count, and resumes queued work.
			// Executing it on this dedicated jthread keeps caller threads responsive.
			m_Pool.reset(requestedThreadCount);
		}
	}

	void JobSystem::recordQueued(JobId id, const Ref<IJob> &job, const Time::TimePoint &now, JobId parentId, JobPriority priority)
	{
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		JobRecord record;
		record.snapshot.id = id;
		record.snapshot.parentId = parentId;
		record.snapshot.name = job->GetName();
		record.snapshot.type = job->GetType();
		record.snapshot.status = JobStatus::Queued;
		record.snapshot.priority = priority;
		record.snapshot.createdAt = now;
		record.job = job;
		record.logs.push_back(JobLogEntry{JobLogLevel::Info, "Queued", now});
		m_Records.emplace(id, std::move(record));
	}

	void JobSystem::runJob(JobId id, Ref<IJob> job)
	{
		ZoneScoped;
		const auto startedAt = Time::Now();
		markRunning(id, startedAt);
		publishStartedEvent(id, *job, startedAt);

		auto onProgress = [this, id](float completedWork, float totalWork) {
			updateProgress(id, completedWork, totalWork);
		};
		auto onStage = [this, id](const std::string &stage) {
			updateStage(id, stage);
		};
		auto onMessage = [this, id](const std::string &message) {
			updateMessage(id, message);
		};
		auto onLog = [this, id](JobLogLevel level, const std::string &message) {
			appendLog(id, level, message);
		};
		auto onCancelQuery = [this, id]() {
			return isCancellationRequested(id);
		};
		auto onSubmitChild = [this, id](const Ref<IJob> &childJob, JobPriority priority) {
			return submitInternal(childJob, priority, id, std::nullopt);
		};
		auto onWaitChild = [this](JobId childId) {
			return waitForJobCooperative(childId);
		};

		JobContext context(
			onProgress,
			onStage,
			onMessage,
			onLog,
			onCancelQuery,
			onSubmitChild,
			onWaitChild);

		WorkerContextGuard workerContext(this, id);

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

	bool JobSystem::waitForJobCooperative(JobId id)
	{
		ZoneScoped;
		while (true)
		{
			if (m_ShutdownRequested.load(std::memory_order_relaxed))
				return false;

			auto child = GetJob(id);
			if (child.has_value() && isFinishedStatus(child->status))
				return true;

			// Avoid deadlock only in true single-worker mode. With 2+ workers,
			// a parent job can safely wait while another worker executes the child.
			if (g_CurrentJobSystem == this && g_CurrentJobId != 0 && m_Pool.get_thread_count() <= 1)
				return false;

			std::this_thread::sleep_for(Time::Milliseconds(1));
		}
	}

	void JobSystem::markRunning(JobId id, const Time::TimePoint &startedAt)
	{
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return;

		it->second.snapshot.status = JobStatus::Running;
		it->second.snapshot.startedAt = startedAt;
		it->second.logs.push_back(JobLogEntry{JobLogLevel::Info, "Running", startedAt});
	}

	void JobSystem::updateProgress(JobId id, float completedWork, float totalWork)
	{
		ZoneScoped;
		std::string stage;
		std::string message;
		{
			std::lock_guard<std::mutex> lock(m_RecordsMutex);
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
		ZoneScoped;
		float completedWork = 0.0f;
		float totalWork = 0.0f;
		std::string message;
		{
			std::lock_guard<std::mutex> lock(m_RecordsMutex);
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
		ZoneScoped;
		float completedWork = 0.0f;
		float totalWork = 0.0f;
		std::string stage;
		{
			std::lock_guard<std::mutex> lock(m_RecordsMutex);
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
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return;

		it->second.logs.push_back(JobLogEntry{level, message, Time::Now()});
	}

	bool JobSystem::isCancellationRequested(JobId id) const
	{
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return true;

		return it->second.cancellationRequested->load(std::memory_order_relaxed);
	}

	void JobSystem::markFinished(JobId id, JobStatus finalStatus, const std::string &errorMessage, const Time::TimePoint &finishedAt)
	{
		ZoneScoped;
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
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
		if (!m_EventBus)
			return std::nullopt;

		return m_EventBus;
	}

	void JobSystem::publishQueuedEvent(JobId id, const IJob &job, const Time::TimePoint &createdAt, JobId parentId, JobPriority priority) const
	{
		auto eventBus = lockEventBus();
		if (!eventBus)
			return;

		(*eventBus)->Queue(JobQueuedEvent{id, job.GetName(), job.GetType(), createdAt, parentId, priority});
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
		ZoneScoped;
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
		ZoneScoped;
		auto shouldStop = [this, &stopToken]() 
		{
			return stopToken.stop_requested() || m_ShutdownRequested.load(std::memory_order_relaxed);
		};

		auto hasWakeCondition = [this, &shouldStop]() 
		{
			return shouldStop() || !m_DelayedSubmissions.empty();
		};


		while (true)
		{
			if (shouldStop())
				return;

			DelayedSubmission submission;
			bool hasReadySubmission = false;

			{
				std::unique_lock<std::mutex> lock(m_DelayedMutex);
				m_DelayedCv.wait(lock, hasWakeCondition);

				if (shouldStop())
					return;

				auto nextIt = std::min_element(
					m_DelayedSubmissions.begin(),
					m_DelayedSubmissions.end());

				if (nextIt == m_DelayedSubmissions.end())
					continue;

				const auto now = Time::NowSteady();
				if (nextIt->dueAt > now)
				{
					m_DelayedCv.wait_until(lock, nextIt->dueAt, shouldStop);
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
		ZoneScoped;
		std::deque<DelayedSubmission> pending;
		{
			std::lock_guard<std::mutex> lock(m_DelayedMutex);
			pending.swap(m_DelayedSubmissions);
		}

		const auto finishedAt = Time::Now();
		for (const auto &submission : pending)
			markFinished(submission.id, JobStatus::Cancelled, "Cancelled by shutdown before dispatch", finishedAt);
	}

	void JobSystem::enqueueForExecution(JobId id, const Ref<IJob> &job, JobPriority priority)
	{
		ZoneScoped;
		if (!job)
			return;

		// Auto-expand queue if needed
		if (IsQueueFull())
		{
			expandJobQueue(m_MaxQueueCapacity + 256);
		}

		m_Pool.detach_task(
			[this, id, job]() { runJob(id, job); },
			toBackendPriority(priority));
	}

	bool JobSystem::PauseAllJobs()
	{
		if (m_ShutdownRequested.load(std::memory_order_relaxed))
			return false;

		std::lock_guard<std::mutex> lock(m_QueueSafetyMutex);
		if (m_AllJobsPaused.exchange(true, std::memory_order_relaxed))
			return true; // Already paused

		m_Pool.pause();
		return true;
	}

	bool JobSystem::ResumeAllJobs()
	{
		if (m_ShutdownRequested.load(std::memory_order_relaxed))
			return false;

		std::lock_guard<std::mutex> lock(m_QueueSafetyMutex);
		if (!m_AllJobsPaused.exchange(false, std::memory_order_relaxed))
			return true; // Already resumed

		m_Pool.unpause();
		return true;
	}

	std::size_t JobSystem::GetQueuedJobCount() const
	{
		std::lock_guard<std::mutex> lock(m_RecordsMutex);
		std::size_t count = 0;
		for (const auto &entry : m_Records)
		{
			if (entry.second.snapshot.status == JobStatus::Queued)
				++count;
		}
		return count;
	}

	std::size_t JobSystem::GetMaxQueueCapacity() const
	{
		std::lock_guard<std::mutex> lock(m_QueueSafetyMutex);
		return m_MaxQueueCapacity;
	}

	bool JobSystem::IsQueueFull() const
	{
		return GetQueuedJobCount() >= GetMaxQueueCapacity();
	}

	bool JobSystem::validateThreadCountChange(std::size_t newThreadCount) const
	{
		const std::size_t resolved = resolveThreadCount(newThreadCount);
		const std::size_t queuedCount = GetQueuedJobCount();
		
		// Sanity check: new thread count should not be much smaller than queued job count
		// Allow flexibility: if queue has many jobs, we don't restrict thread reduction too much
		if (queuedCount > 0 && resolved < 1)
			return false;

		return true;
	}

	void JobSystem::expandJobQueue(std::size_t newCapacity)
	{
		std::lock_guard<std::mutex> lock(m_QueueSafetyMutex);
		if (newCapacity > m_MaxQueueCapacity)
			m_MaxQueueCapacity = newCapacity;
	}

	void JobSystem::storeQueuedJobsTemporarily()
	{
		// Jobs are kept in m_Records and remain queued
		// The pause() call ensures they won't execute until unpause()
		std::lock_guard<std::mutex> lock(m_QueueSafetyMutex);
		m_TemporaryJobStorage.clear();
	}

	void JobSystem::restoreQueuedJobsFromTemporary()
	{
		// Jobs are already in the queue after unpause()
		std::lock_guard<std::mutex> lock(m_QueueSafetyMutex);
		m_TemporaryJobStorage.clear();
	}

	void JobSystem::clearTemporaryJobStorage()
	{
		std::lock_guard<std::mutex> lock(m_QueueSafetyMutex);
		m_TemporaryJobStorage.clear();
	}

	bool JobSystem::SafeSetThreadCount(std::size_t newThreadCount, std::string &outMessage)
	{
		if (m_ShutdownRequested.load(std::memory_order_relaxed))
		{
			outMessage = "Cannot change thread count: JobSystem is shutting down";
			return false;
		}

		const std::size_t resolved = resolveThreadCount(newThreadCount);
		const std::size_t currentThreadCount = m_Pool.get_thread_count();

		if (resolved == currentThreadCount)
		{
			outMessage = "Thread count unchanged (already " + std::to_string(resolved) + ")";
			return true;
		}

		// Step 1: Validate the new thread count
		if (!validateThreadCountChange(resolved))
		{
			outMessage = "Thread count validation failed: new count (" + std::to_string(resolved) +
						 ") is invalid for queued jobs (" + std::to_string(GetQueuedJobCount()) + ")";
			return false;
		}

		// Step 2: Pause all jobs
		if (!PauseAllJobs())
		{
			outMessage = "Failed to pause jobs";
			return false;
		}

		// Step 3: Store queued jobs temporarily
		storeQueuedJobsTemporarily();

		// Step 4: Change thread count
		if (!SetThreadCount(resolved))
		{
			outMessage = "Failed to set thread count";
			(void)ResumeAllJobs(); // Suppress [[nodiscard]] warning
			clearTemporaryJobStorage();
			return false;
		}

		// Wait briefly for the change to take effect
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		// Step 5: Restore queued jobs
		restoreQueuedJobsFromTemporary();

		// Step 6: Resume jobs
		if (!ResumeAllJobs())
		{
			outMessage = "Failed to resume jobs";
			return false;
		}

		outMessage = "Thread count changed from " + std::to_string(currentThreadCount) + " to " + std::to_string(resolved);
		return true;
	}
} // namespace DefectStudio
