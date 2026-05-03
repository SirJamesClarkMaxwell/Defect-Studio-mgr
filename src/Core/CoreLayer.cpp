#include "Core/dspch.hpp"


#include <algorithm>
#include <functional>

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/CoreLayer.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobSystemConfigEvents.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeCoreLayer(
			EventBus &bus,
			CoreLayer &layer,
			void (CoreLayer::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &layer));
		}

		std::size_t resolveWorkerThreadCount(const JobsConfig &jobsConfig)
		{
			const std::size_t urgentWorkerCount = static_cast<std::size_t>(jobsConfig.reserveUrgentWorker ? 1 : 0);
			return static_cast<std::size_t>(std::max(1, jobsConfig.defaultWorkerThreadCount)) + urgentWorkerCount;
		}
	}

	CoreLayer::CoreLayer() : Layer("CoreLayer")
	{
	}

	void CoreLayer::OnAttach()
	{
		DS_LOG_INFO("CoreLayer attached");
	}

	void CoreLayer::OnDetach()
	{
		ShutdownSystems();
		DS_LOG_INFO("CoreLayer detached");
	}

	void CoreLayer::OnUpdate(float deltaTime)
	{
		m_AccumulatedTime += deltaTime;
		if (m_EventBus)
			m_EventBus->ProcessQueue();
	}

	bool CoreLayer::InitializeSystems(Ref<EventBus> eventBus, const JobsConfig &jobsConfig)
	{
		if (m_SystemsInitialized)
		{
			DS_LOG_WARN("CoreLayer::InitializeSystems called more than once; call ignored");
			return true;
		}

		m_EventBus = std::move(eventBus);
		if (!m_EventBus)
		{
			DS_LOG_ERROR("CoreLayer::InitializeSystems requires a valid EventBus");
			return false;
		}

		const std::size_t workerThreadCount = resolveWorkerThreadCount(jobsConfig);
		DS_LOG_INFO(
			"Init: JobSystem worker_count={} (base={} reserve_urgent={})",
			workerThreadCount,
			jobsConfig.defaultWorkerThreadCount,
			jobsConfig.reserveUrgentWorker);
		m_JobSystem = CreateRef<JobSystem>(m_EventBus, workerThreadCount);

		DS_LOG_INFO("Init: ProgressTracker");
		m_ProgressTracker = CreateRef<ProgressTracker>(m_EventBus);

		AddSubscription(subscribeCoreLayer<JobSystemConfigAppliedEvent>(
			*m_EventBus,
			*this,
			&CoreLayer::onJobSystemConfigApplied));

		m_SystemsInitialized = true;
		DS_LOG_INFO(
			"Init complete: runtime services worker_count={} (base={} reserve_urgent={})",
			m_JobSystem->GetThreadCount(),
			jobsConfig.defaultWorkerThreadCount,
			jobsConfig.reserveUrgentWorker);
		return true;
	}

	void CoreLayer::ShutdownSystems()
	{
		if (!m_SystemsInitialized)
			return;

		DS_LOG_INFO("Shutdown: ProgressTracker");
		m_ProgressTracker.reset();
		DS_LOG_INFO("Shutdown: JobSystem");
		m_JobSystem.reset();
		ClearSubscriptions();
		m_EventBus.reset();
		m_SystemsInitialized = false;
	}

	EventBus &CoreLayer::GetEventBus()
	{
		DS_ASSERT(m_EventBus != nullptr, "EventBus is not initialized");
		return *m_EventBus;
	}

	JobSystem &CoreLayer::GetJobSystem()
	{
		DS_ASSERT(m_JobSystem != nullptr, "JobSystem is not initialized");
		return *m_JobSystem;
	}

	ProgressTracker &CoreLayer::GetProgressTracker()
	{
		DS_ASSERT(m_ProgressTracker != nullptr, "ProgressTracker is not initialized");
		return *m_ProgressTracker;
	}

	WeakRef<JobSystem> CoreLayer::GetJobSystemHandle() const
	{
		if (m_JobSystem == nullptr)
			return {};
		return CreateWeakRef(m_JobSystem);
	}

	WeakRef<ProgressTracker> CoreLayer::GetProgressTrackerHandle() const
	{
		if (m_ProgressTracker == nullptr)
			return {};
		return CreateWeakRef(m_ProgressTracker);
	}

	void CoreLayer::applyJobConfig(const JobsConfig &jobsConfig)
	{
		if (m_JobSystem == nullptr)
		{
			DS_LOG_WARN("CoreLayer: job config apply skipped because JobSystem is unavailable");
			return;
		}

		const std::size_t targetThreadCount = resolveWorkerThreadCount(jobsConfig);
		const std::size_t previousThreadCount = m_JobSystem->GetThreadCount();
		if (!m_JobSystem->SetThreadCount(targetThreadCount))
		{
			DS_LOG_WARN(
				"CoreLayer: failed to apply worker count target={} previous={}",
				targetThreadCount,
				previousThreadCount);
			return;
		}

		DS_LOG_INFO(
			"CoreLayer: worker count applied previous={} target={} (base={} reserve_urgent={})",
			previousThreadCount,
			targetThreadCount,
			jobsConfig.defaultWorkerThreadCount,
			jobsConfig.reserveUrgentWorker);
	}

	void CoreLayer::onJobSystemConfigApplied(const JobSystemConfigAppliedEvent &event)
	{
		DS_LOG_INFO("CoreLayer received job system config applied event");
		applyJobConfig(event.jobs);
	}
} // namespace DefectStudio
