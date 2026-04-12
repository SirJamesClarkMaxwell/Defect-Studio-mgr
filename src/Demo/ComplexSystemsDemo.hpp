#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <vector>

#include "Core/Event.hpp"
#include "Core/EventBus.hpp"
#include "Core/ProgressTracker.hpp"

namespace DefectStudio::Demo
{
	struct PipelineTask
	{
		int id = 0;
		std::shared_ptr<std::atomic_int> progress;
		std::future<int> future;
		std::optional<ProgressTracker::ProgressId> progressId;
	};
	
	class ComplexSystemsDemo
	{
	public:
		ComplexSystemsDemo();
		~ComplexSystemsDemo();

		void Render();

	private:

		void StartPipeline();
		void SyncPipeline();
		int DoPipelineWork(int taskId, std::shared_ptr<std::atomic_int> progress);
		bool HandleLocalDispatch(WindowResizeEvent &event);
		void OnPipelineStatus(const struct PipelineStatusBusEvent &event);

		std::vector<PipelineTask> m_PipelineTasks;
		EventBus::SubscriptionId m_PipelineSub = 0;
		int m_NextTaskId = 1;
		int m_DispatchedCompletions = 0;
		int m_ReceivedBusUpdates = 0;
		std::string m_LastBusMessage;
	};
} // namespace DefectStudio::Demo
