#include "Core/dspch.hpp"

#include <chrono>
#include <thread>

#include <imgui.h>

#include "App/Application.hpp"
#include "Core/Event.hpp"
#include "Demo/ComplexSystemsDemo.hpp"
#include "Demo/DemoEvents.hpp"

namespace DefectStudio::Demo
{
	ComplexSystemsDemo::ComplexSystemsDemo()
	{
		m_PipelineSub = Application::Get().GetEventBus().Subscribe<PipelineStatusBusEvent>(
		    std::bind(&ComplexSystemsDemo::OnPipelineStatus, this, std::placeholders::_1));
	}

	ComplexSystemsDemo::~ComplexSystemsDemo()
	{
		Application::Get().GetEventBus().Unsubscribe<PipelineStatusBusEvent>(m_PipelineSub);
	}

	void ComplexSystemsDemo::Render()
	{
		SyncPipeline();

		ImGui::Begin("Demo/Complex Flow (Jobs + Dispatcher + Bus)");
		ImGui::TextUnformatted("1) enqueue batch jobs 2) dispatch local event on completion 3) publish global status event");

		if (ImGui::Button("Start Batch Of 3 Jobs"))
			StartPipeline();

		ImGui::Text("Local dispatch completions: %d", m_DispatchedCompletions);
		ImGui::Text("Bus updates received: %d", m_ReceivedBusUpdates);
		ImGui::TextWrapped("Last bus message: %s", m_LastBusMessage.empty() ? "<none>" : m_LastBusMessage.c_str());

		for (const auto &task : m_PipelineTasks)
		{
			const float fraction = task.progress ? static_cast<float>(task.progress->load()) / 100.0f : 0.0f;
			std::string label = "Pipeline Task " + std::to_string(task.id);
			ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label.c_str());
		}

		ImGui::End();
	}

	void ComplexSystemsDemo::StartPipeline()
	{
		auto &app = Application::Get();
		auto &jobs = app.GetJobSystem();
		auto &tracker = app.GetProgressTracker();

		for (int i = 0; i < 3; ++i)
		{
			PipelineTask task;
			task.id = m_NextTaskId++;
			task.progress = std::make_shared<std::atomic_int>(0);
			task.progressId = tracker.Register("Pipeline Task " + std::to_string(task.id), 100);
			task.future = jobs.Submit(std::bind(&ComplexSystemsDemo::DoPipelineWork, this, task.id, task.progress));
			m_PipelineTasks.push_back(std::move(task));
		}
	}

	void ComplexSystemsDemo::SyncPipeline()
	{
		auto &app = Application::Get();
		auto &tracker = app.GetProgressTracker();
		auto &bus = app.GetEventBus();

		for (auto &task : m_PipelineTasks)
		{
			if (task.progressId.has_value() && task.progress)
				tracker.Report(*task.progressId, static_cast<std::size_t>(task.progress->load()));

			if (!task.future.valid())
				continue;
			if (task.future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
				continue;

			const int finishedTaskId = task.future.get();
			if (task.progressId.has_value())
				tracker.Finish(*task.progressId);

			WindowResizeEvent completionSignal(finishedTaskId, m_DispatchedCompletions);
			Event &baseEvent = completionSignal;
			EventDispatcher dispatcher(baseEvent);
			dispatcher.Dispatch<WindowResizeEvent>(std::bind(&ComplexSystemsDemo::HandleLocalDispatch, this, std::placeholders::_1));

			bus.Publish(PipelineStatusBusEvent{"Task " + std::to_string(finishedTaskId) + " finished and published to EventBus"});
		}

		m_PipelineTasks.erase(
		    std::remove_if(
		        m_PipelineTasks.begin(),
		        m_PipelineTasks.end(),
		        [](PipelineTask &task) {
				if (!task.future.valid())
					return true;
				return task.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
			}),
		    m_PipelineTasks.end());
	}

	int ComplexSystemsDemo::DoPipelineWork(int taskId, std::shared_ptr<std::atomic_int> progress)
	{
		for (int i = 1; i <= 100; ++i)
		{
			progress->store(i);
			std::this_thread::sleep_for(std::chrono::milliseconds(8 + (taskId % 3) * 7));
		}
		return taskId;
	}

	bool ComplexSystemsDemo::HandleLocalDispatch(WindowResizeEvent &event)
	{
		(void)event;
		++m_DispatchedCompletions;
		return true;
	}

	void ComplexSystemsDemo::OnPipelineStatus(const PipelineStatusBusEvent &event)
	{
		++m_ReceivedBusUpdates;
		m_LastBusMessage = event.message;
	}
} // namespace DefectStudio::Demo
