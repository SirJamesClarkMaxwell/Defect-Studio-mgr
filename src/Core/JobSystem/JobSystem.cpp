#include "Core/dspch.hpp"

#include "Core/JobSystem/JobSystem.hpp"


#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Time.hpp"
#include "App/Events/ApplicationEvents.hpp"

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
		  m_Pool(resolveThreadCount(threadCount))
	{
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
		auto priority = toBackendPriority(priority);
		m_Pool.detach_task([this, id, job]() {runJob(id, job);},priority);

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
			m_Pool.wait();
		});
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
		record.logs.push_back(JobLogEntry{JobLogLevel::Info, "Queued", now});
		m_Records.emplace(id, std::move(record));
	}

	void JobSystem::runJob(JobId id, Ref<IJob> job)
	{
		const auto startedAt = Time::Now();
		markRunning(id, startedAt);
		publishStartedEvent(id, *job);

		JobContext context(
			[this, id](float completedWork, float totalWork) { updateProgress(id, completedWork, totalWork); },
			[this, id](const std::string &stage) { updateStage(id, stage); },
			[this, id](const std::string &message) { updateMessage(id, message); },
			[this, id](JobLogLevel level, const std::string &message) { appendLog(id, level, message); },
			[this, id]() { return isCancellationRequested(id); });

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
		publishFinishedEvent(id, *job, finalStatus, errorMessage);
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
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return;

		it->second.snapshot.completedWork = completedWork;
		it->second.snapshot.totalWork = totalWork;
	}

	void JobSystem::updateStage(JobId id, const std::string &stage)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return;

		it->second.snapshot.currentStage = stage;
	}

	void JobSystem::updateMessage(JobId id, const std::string &message)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Records.find(id);
		if (it == m_Records.end())
			return;

		it->second.snapshot.currentMessage = message;
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

	void JobSystem::publishStartedEvent(JobId id, const IJob &job) const
	{
		auto eventBus = lockEventBus();
		if (!eventBus)
			return;

		(*eventBus)->Queue(JobStartedEvent{id, job.GetName(), job.GetType()});
	}

	void JobSystem::publishFinishedEvent(JobId id, const IJob &job, JobStatus status, const std::string &errorMessage) const
	{
		auto eventBus = lockEventBus();
		if (!eventBus)
			return;

		if (status == JobStatus::Completed)
			(*eventBus)->Queue(JobCompletedEvent{id, job.GetName(), job.GetType()});
		else if (status == JobStatus::Cancelled)
			(*eventBus)->Queue(JobCancelledEvent{id, job.GetName(), job.GetType()});
		else
			(*eventBus)->Queue(JobFailedEvent{id, job.GetName(), job.GetType(), errorMessage});
	}
} // namespace DefectStudio
