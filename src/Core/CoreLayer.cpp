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
		InitializeSystems();
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

	bool CoreLayer::InitializeSystems()
	{
		if (m_SystemsInitialized)
		{
			DS_LOG_WARN("CoreLayer::InitializeSystems called more than once; call ignored");
			return true;
		}

		if (!InitializeEventBusSystem())
			return false;

		DS_LOG_INFO("Init: JobSystem");
		m_JobSystem = CreateUnique<JobSystem>(CreateWeakRef(m_EventBus));

		DS_LOG_INFO("Init: ProgressTracker");
		m_ProgressTracker = CreateUnique<ProgressTracker>();

		m_SystemsInitialized = true;
		DS_LOG_INFO("Init complete: runtime services");
		return true;
	}

	bool CoreLayer::InitializeEventBusSystem()
	{
		if (m_EventBus)
			return true;

		DS_LOG_INFO("Init: EventBus");
		m_EventBus = CreateRef<EventBus>();
		return m_EventBus != nullptr;
	}

	void CoreLayer::ShutdownSystems()
	{
		if (!m_SystemsInitialized)
			return;

		DS_LOG_INFO("Shutdown: ProgressTracker");
		m_ProgressTracker.reset();
		DS_LOG_INFO("Shutdown: JobSystem");
		m_JobSystem.reset();
		ShutdownEventBusSystem();
		m_SystemsInitialized = false;
	}

	void CoreLayer::ShutdownEventBusSystem()
	{
		if (!m_EventBus)
			return;

		DS_LOG_INFO("Shutdown: EventBus");
		m_EventBus.reset();
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
} // namespace DefectStudio
