#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

namespace DefectStudio
{
	class CoreLayer final : public Layer
	{
	public:
		CoreLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

		bool InitializeSystems(WeakRef<EventBus> eventBus);
		void ShutdownSystems();

		EventBus &GetEventBus();
		JobSystem &GetJobSystem();
		ProgressTracker &GetProgressTracker();

	private:
		float m_AccumulatedTime = 0.0f;
		bool m_SystemsInitialized = false;
		WeakRef<EventBus> m_EventBus;
		Unique<JobSystem> m_JobSystem;
		Unique<ProgressTracker> m_ProgressTracker;
	};
} // namespace DefectStudio
