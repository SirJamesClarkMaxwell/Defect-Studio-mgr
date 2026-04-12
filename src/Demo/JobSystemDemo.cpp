#include "Core/dspch.hpp"

#include <chrono>
#include <thread>

#include <imgui.h>

#include "App/Application.hpp"
#include "Demo/JobSystemDemo.hpp"

namespace DefectStudio::Demo
{
	void JobSystemDemo::Render()
	{
		SyncTasks();

		ImGui::Begin("Demo/JobSystem + ProgressTracker");
		ImGui::TextUnformatted("Queue multiple tasks. Each task has its own cancellation and progress entry.");

		ImGui::Spacing();
		if (ImGui::Button("Enqueue Task"))
			EnqueueTask();

		ImGui::SameLine();
		if (ImGui::Button("Cancel All"))
		{
			for (auto &task : m_Tasks)
				task.cancelSource.Cancel();
		}

		const auto activeCount = static_cast<int>(std::count_if(
		    m_Tasks.begin(),
		    m_Tasks.end(),
		    [](const TaskEntry &task) { return task.status == "running" || task.status == "queued"; }));
		ImGui::Text("Tasks total/history: %d", static_cast<int>(m_Tasks.size()));
		ImGui::Text("Tasks active: %d", activeCount);
		ImGui::Text("Pool queued: %zu", Application::Get().GetJobSystem().GetQueuedJobs());
		ImGui::Text("Pool running: %zu", Application::Get().GetJobSystem().GetRunningJobs());
		ImGui::Text("Total completed: %d", m_TotalCompleted);
		ImGui::Text("Total cancelled: %d", m_TotalCancelled);

		auto globalProgress = Application::Get().GetProgressTracker().Snapshot();
		if (ImGui::CollapsingHeader("Global task pool (ProgressTracker)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const auto &entry : globalProgress)
			{
				const float fraction = entry.totalWork == 0 ? 0.0f : static_cast<float>(entry.completedWork) / static_cast<float>(entry.totalWork);
				std::string label = entry.label + (entry.finished ? " [done]" : " [active]");
				ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label.c_str());
			}
		}

		auto renderTaskRow = [](TaskEntry &task) {
			ImGui::Separator();
			ImGui::Text("Task %d status: %s result: %d", task.id, task.status.c_str(), task.result);
			if (task.progress)
			{
				const float fraction = static_cast<float>(task.progress->load()) / 100.0f;
				std::string label = "Task " + std::to_string(task.id);
				ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label.c_str());
			}

			if (task.status == "running" || task.status == "queued")
			{
				std::string cancelLabel = "Cancel Task " + std::to_string(task.id);
				if (ImGui::Button(cancelLabel.c_str()))
					task.cancelSource.Cancel();
			}
		};

		if (ImGui::CollapsingHeader("Active tasks", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (auto &task : m_Tasks)
			{
				if (task.status == "running" || task.status == "queued")
					renderTaskRow(task);
			}
		}

		if (ImGui::CollapsingHeader("Completed tasks", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (auto &task : m_Tasks)
			{
				if (task.status == "completed")
					renderTaskRow(task);
			}
}

		if (ImGui::CollapsingHeader("Cancelled tasks", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (auto &task : m_Tasks)
			{
				if (task.status == "cancelled")
					renderTaskRow(task);
			}
		}

		ImGui::End();
	}

	void JobSystemDemo::EnqueueTask()
	{
		auto progress = std::make_shared<std::atomic_int>(0);
		TaskEntry task;
		task.id = m_NextTaskId++;
		task.progress = progress;

		auto &app = Application::Get();
		auto &tracker = app.GetProgressTracker();
		task.progressId = tracker.Register("Demo Task " + std::to_string(task.id), 100);
		task.status = "running";

		task.future = app.GetJobSystem().SubmitCancelable(
		    task.cancelSource.GetToken(),
		    std::bind(&JobSystemDemo::DoWork, this, std::placeholders::_1, task.id, progress));

		m_Tasks.push_back(std::move(task));
	}

	void JobSystemDemo::SyncTasks()
	{
		auto &tracker = Application::Get().GetProgressTracker();
		for (auto &task : m_Tasks)
		{
			if (task.progressId.has_value() && task.progress)
				tracker.Report(*task.progressId, static_cast<std::size_t>(task.progress->load()));

			if (!IsTaskReady(task))
				continue;

			task.result = task.future.get();
			task.status = task.result >= 0 ? "completed" : "cancelled";
			if (task.result >= 0)
				++m_TotalCompleted;
			else
				++m_TotalCancelled;

			if (task.progressId.has_value())
				tracker.Finish(*task.progressId);
		}

		// Keep completed/cancelled tasks as history for inspection.
	}

	int JobSystemDemo::DoWork(const CancellationToken &token, int taskId, std::shared_ptr<std::atomic_int> progress)
	{
		const int delayMs = 4 + (taskId % 4) * 5;
		for (int i = 1; i <= 100; ++i)
		{
			if (token.IsCancellationRequested())
				return -1;

			progress->store(i);
			std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
		}

		return 1000 + taskId;
	}

	bool JobSystemDemo::IsTaskReady(TaskEntry &task)
	{
		if (!task.future.valid())
			return false;

		return task.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
	}
} // namespace DefectStudio::Demo
