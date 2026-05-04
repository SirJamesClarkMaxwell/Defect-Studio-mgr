#pragma once

#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class EventBus;
	class JobSystem;

	class TaskMonitorWindow final : public IPanel
	{
	public:
		explicit TaskMonitorWindow(Ref<EventBus> eventBus = {},
		                          WeakRef<JobSystem> jobSystem = {},
		                          std::string title = "Task Monitor",
		                          bool visibleByDefault = true);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		char m_DummyJobName[64] = "DummyJob";
		int m_DummyJobSteps = 500;
		int m_DummyJobDelayMs = 10;
		int m_SubtaskChainCount = 4;
		JobPriority m_DummyJobPriority = JobPriority::Normal;
		float m_StatusIconWidth = 0.0f;
		Ref<EventBus> m_EventBus;
		WeakRef<JobSystem> m_JobSystem;
	};
} // namespace DefectStudio
