#include "Core/dspch.hpp"

#include <algorithm>

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/TestJobs/TestJobs.hpp"
#include "Core/Utils/Logger.hpp"
#include "Presentation/Panels/TaskMonitorWindow.hpp"

namespace DefectStudio
{
	class UITaskMonitorSubtaskJob final : public IJob
	{
	public:
		UITaskMonitorSubtaskJob(std::string name, int chainCount)
			: m_Name(std::move(name)), m_ChainCount(std::max(1, chainCount))
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return m_Name;
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "UITaskMonitorSubtaskJob";
		}

		void Execute(JobContext &context) override
		{
			context.SetStage("submitting-subtasks");
			for (int i = 0; i < m_ChainCount; ++i)
			{
				auto child = CreateRef<SleepJob>(m_Name + "::child-" + std::to_string(i + 1), 30, Time::Milliseconds(8));
				const auto childId = context.SubmitJob(child, JobPriority::Normal);
				if (childId == 0)
				{
					context.LogError("Could not submit child");
					break;
				}
				context.SetProgress(static_cast<float>(i + 1), static_cast<float>(m_ChainCount));
				context.SetMessage("child job id: " + std::to_string(childId));
			}
			context.SetStage("done");
		}

	private:
		std::string m_Name;
		int m_ChainCount = 1;
	};

	TaskMonitorWindow::TaskMonitorWindow(Ref<EventBus> eventBus, WeakRef<JobSystem> jobSystem, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_EventBus(std::move(eventBus)),
		  m_JobSystem(std::move(jobSystem))
	{
	}

	Ref<IPanel> TaskMonitorWindow::Clone() const
	{
		return CreateRef<TaskMonitorWindow>(*this);
	}

	void TaskMonitorWindow::Render()
	{
		auto jobSystem = m_JobSystem.lock();

		if (!IsVisible())
			return;

		bool visible = IsVisible();
		ImGui::SetNextWindowSize(ImVec2(460.0f, 320.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			SetVisible(visible);
			ImGui::End();
			return;
		}
		SetVisible(visible);

		ImGui::TextUnformatted("Submit a dummy job to the global job system.");
		ImGui::InputText("Name", m_DummyJobName, IM_ARRAYSIZE(m_DummyJobName));
		ImGui::InputInt("Steps", &m_DummyJobSteps);
		ImGui::InputInt("Step delay (ms)", &m_DummyJobDelayMs);
		ImGui::InputInt("Sequential subtasks", &m_SubtaskChainCount);

		m_DummyJobSteps = std::max(1, m_DummyJobSteps);
		m_DummyJobDelayMs = std::max(1, m_DummyJobDelayMs);
		m_SubtaskChainCount = std::max(1, m_SubtaskChainCount);

		const char *priorityLabels[] = {"Lowest", "Low", "Normal", "High", "Highest"};
		int priorityIndex = static_cast<int>(m_DummyJobPriority);
		if (ImGui::Combo("Priority", &priorityIndex, priorityLabels, IM_ARRAYSIZE(priorityLabels)))
			m_DummyJobPriority = static_cast<JobPriority>(std::clamp(priorityIndex, 0, 4));

		if (ImGui::Button("Submit Dummy Job"))
		{
			if (m_EventBus != nullptr)
			{
				m_EventBus->Queue(JobSubmitRequested{
					CreateRef<SleepJob>(std::string(m_DummyJobName), m_DummyJobSteps, Time::Milliseconds{m_DummyJobDelayMs}),
					m_DummyJobPriority,
					"TaskMonitorWindow"});
				DS_LOG_INFO("TaskMonitor queued dummy job name=\"{}\" steps={} delay_ms={} priority={}", m_DummyJobName, m_DummyJobSteps, m_DummyJobDelayMs, static_cast<int>(m_DummyJobPriority));
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Submit Subtask Demo"))
		{
			if (m_EventBus != nullptr)
			{
				m_EventBus->Queue(JobSubmitRequested{
					CreateRef<UITaskMonitorSubtaskJob>(std::string(m_DummyJobName) + "::parent", m_SubtaskChainCount),
					m_DummyJobPriority,
					"TaskMonitorWindow"});
				DS_LOG_INFO("TaskMonitor queued subtask demo name=\"{}\" chain={} priority={}", m_DummyJobName, m_SubtaskChainCount, static_cast<int>(m_DummyJobPriority));
			}
		}

		ImGui::SameLine();
		ImGui::Text("Threads: %llu", static_cast<unsigned long long>(jobSystem != nullptr ? jobSystem->GetThreadCount() : 0));
		ImGui::TextWrapped("Submitted subtask chain should be visible in Progress Monitor through parentId links.");
		if (jobSystem == nullptr)
			ImGui::TextDisabled("JobSystem service unavailable.");
		ImGui::End();
	}
} // namespace DefectStudio
