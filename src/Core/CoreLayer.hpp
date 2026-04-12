#pragma once

#include "Core/EventBus.hpp"
#include "Core/JobSystem.hpp"
#include "Core/Layer.hpp"
#include "Core/Memory.hpp"
#include "Core/ProgressTracker.hpp"

namespace DefectStudio
{
	class CoreLayer final : public Layer
	{
	public:
		CoreLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

		bool InitializeSystems();
		void ShutdownSystems();

		EventBus &GetEventBus();
		JobSystem &GetJobSystem();
		ProgressTracker &GetProgressTracker();

	private:
		float m_AccumulatedTime = 0.0f;
		bool m_SystemsInitialized = false;
		Unique<EventBus> m_EventBus;
		Unique<JobSystem> m_JobSystem;
		Unique<ProgressTracker> m_ProgressTracker;
	};
} // namespace DefectStudio
