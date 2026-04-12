#pragma once

#include <atomic>
#include <future>
#include <optional>
#include <vector>

#include "Core/JobSystem.hpp"
#include "Core/ProgressTracker.hpp"

namespace DefectStudio::Demo
{
	struct TaskEntry
	{
		int id = 0;
		std::shared_ptr<std::atomic_int> progress;
		CancellationSource cancelSource;
		std::future<int> future;
		std::optional<ProgressTracker::ProgressId> progressId;
		std::string status = "queued";
		int result = 0;
	};
	class JobSystemDemo
	{
	public:
		void Render();

	private:

		void EnqueueTask();
		void SyncTasks();
		int DoWork(const CancellationToken &token, int taskId, std::shared_ptr<std::atomic_int> progress);

		int m_NextTaskId = 1;
		std::vector<TaskEntry> m_Tasks;
		int m_TotalCompleted = 0;
		int m_TotalCancelled = 0;

		static bool IsTaskReady(TaskEntry &task);
	};
} // namespace DefectStudio::Demo
