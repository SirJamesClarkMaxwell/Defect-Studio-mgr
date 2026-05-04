#pragma once

#include "Core/Utils/Memory.hpp"
#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobSystemConfig.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

namespace DefectStudio
{
	struct JobSystemConfigAppliedEvent;
	struct JobCancelRequested;
	struct JobHistoryRemoveRequested;
	struct JobResetRequested;
	struct JobResumeRequested;
	struct JobRetryRequested;
	struct JobSubmitRequested;

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
		void onJobSystemConfigApplied(const JobSystemConfigAppliedEvent &event);
		void onJobSubmitRequested(const JobSubmitRequested &event);
		void onJobCancelRequested(const JobCancelRequested &event);
		void onJobResumeRequested(const JobResumeRequested &event);
		void onJobResetRequested(const JobResetRequested &event);
		void onJobRetryRequested(const JobRetryRequested &event);
		void onJobHistoryRemoveRequested(const JobHistoryRemoveRequested &event);

	private:
		float m_AccumulatedTime = 0.0f;
		bool m_SystemsInitialized = false;
		Ref<EventBus> m_EventBus;
		Ref<JobSystem> m_JobSystem;
		Ref<ProgressTracker> m_ProgressTracker;
	};
} // namespace DefectStudio
