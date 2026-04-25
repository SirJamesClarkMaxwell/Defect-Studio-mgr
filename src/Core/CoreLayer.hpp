#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

namespace DefectStudio
{
	struct JobsConfig;

	namespace AppEvents::Config
	{
		struct Applied;
	}

	class CoreLayer final : public Layer, public EventReceiver
	{
	public:
		CoreLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

		bool InitializeSystems(Ref<EventBus> eventBus, const JobsConfig &jobsConfig);
		void ShutdownSystems();

		EventBus &GetEventBus();
		JobSystem &GetJobSystem();
		ProgressTracker &GetProgressTracker();
		[[nodiscard]] WeakRef<JobSystem> GetJobSystemHandle() const;
		[[nodiscard]] WeakRef<ProgressTracker> GetProgressTrackerHandle() const;

	private:
		void applyJobConfig(const JobsConfig &jobsConfig);
		void onConfigApplied(const AppEvents::Config::Applied &event);

	private:
		float m_AccumulatedTime = 0.0f;
		bool m_SystemsInitialized = false;
		Ref<EventBus> m_EventBus;
		Ref<JobSystem> m_JobSystem;
		Ref<ProgressTracker> m_ProgressTracker;
	};
} // namespace DefectStudio
