#include "Core/dspch.hpp"

#include "Core/CoreLayer.hpp"

#include "Core/Utils/Assert.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

namespace DefectStudio
{
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

	bool CoreLayer::InitializeSystems(Ref<EventBus> eventBus)
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

		DS_LOG_INFO("Init: JobSystem");
		m_JobSystem = CreateRef<JobSystem>(m_EventBus);

		DS_LOG_INFO("Init: ProgressTracker");
		m_ProgressTracker = CreateRef<ProgressTracker>(m_EventBus);

		m_SystemsInitialized = true;
		DS_LOG_INFO("Init complete: runtime services");
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
} // namespace DefectStudio
