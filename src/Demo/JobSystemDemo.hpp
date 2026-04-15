#pragma once

#include <atomic>
#include <deque>
#include <unordered_map>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/SubscriptionHandle.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio::Demo
{
	class JobSystemDemo
	{
	public:
		JobSystemDemo();
		~JobSystemDemo();

		void Render();

	private:
		void submitSleepJob(int steps, int delayMs, JobPriority priority);
		void submitFailingJob();
		void submitBulkJobs(int count, int steps, int delayMs, JobPriority priority);

		void syncProgressTracker();
		void renderControls();
		void renderEventBusPanel();
		void renderProgressPanel();
		void renderJobsPanel();
		void renderSelectedJobPanel();
		void appendLifecycleEvent(std::string eventLine);

	private:
		// Demo lane is intentionally isolated from Application services.
		Ref<EventBus> m_DemoEventBus;
		Unique<JobSystem> m_DemoJobSystem;

		// Tracker is driven by snapshots from the demo-owned JobSystem.
		ProgressTracker m_ProgressTracker;
		std::unordered_map<JobId, ProgressTracker::ProgressId> m_ProgressIds;

		std::vector<JobId> m_KnownJobs;
		Ref<std::atomic_bool> m_PauseRequested;

		JobId m_SelectedJobId = 0;
		int m_JobCounter = 0;

		int m_NewJobSteps = 30;
		int m_NewJobDelayMs = 10;
		int m_BulkJobCount = 10;
		int m_DelayedStartMs = 250;
		int m_ThreadCountInput = 0;
		std::size_t m_CurrentThreadCount = 0;
		bool m_ShowControls = true;
		bool m_ShowEventBus = true;
		bool m_ShowProgress = true;
		bool m_ShowWaiting = true;
		bool m_ShowFinished = true;
		bool m_ShowSelectedJob = true;
		bool m_ShowLogs = true;
		std::deque<std::string> m_LifecycleEvents;

		SubscriptionHandle m_StartedSubscription;
		SubscriptionHandle m_CompletedSubscription;
		SubscriptionHandle m_CancelledSubscription;
		SubscriptionHandle m_FailedSubscription;
	};
} // namespace DefectStudio::Demo
