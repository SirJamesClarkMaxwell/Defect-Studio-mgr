#include "Core/dspch.hpp"

#include <algorithm>

#include <imgui.h>

#include "App/Application.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/TestJobs/TestJobs.hpp"
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
				const auto childId = context.SubmitJobSequential(child, JobPriority::Normal);
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

	TaskMonitorWindow::TaskMonitorWindow(std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault)
	{
	}

	Ref<IPanel> TaskMonitorWindow::Clone() const
	{
		return CreateRef<TaskMonitorWindow>(*this);
	}

	void TaskMonitorWindow::Render()
	{
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
			auto &jobSystem = Application::Get().GetJobSystem();
			(void)jobSystem.Submit(CreateRef<SleepJob>(std::string(m_DummyJobName), m_DummyJobSteps, Time::Milliseconds{m_DummyJobDelayMs}), m_DummyJobPriority);
		}
		ImGui::SameLine();
		if (ImGui::Button("Submit Subtask Demo"))
		{
			auto &jobSystem = Application::Get().GetJobSystem();
			(void)jobSystem.Submit(CreateRef<UITaskMonitorSubtaskJob>(std::string(m_DummyJobName) + "::parent", m_SubtaskChainCount), m_DummyJobPriority);
		}

		ImGui::SameLine();
		ImGui::Text("Threads: %llu", static_cast<unsigned long long>(Application::Get().GetJobSystem().GetThreadCount()));
		ImGui::TextWrapped("Submitted subtask chain should be visible in Progress Monitor through parentId links.");
		ImGui::End();
	}
} // namespace DefectStudio
