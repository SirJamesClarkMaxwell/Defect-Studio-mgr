#pragma once

#include <vector>

#include "Core/Layer.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

namespace DefectStudio
{
	class EditorLayer final : public Layer
	{
	public:
		EditorLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnImGuiRender() override;

	private:
		void renderMainMenuBar();
		void renderProgressMonitorWindow();
		void renderTaskMonitorWindow();
		void renderSettingsWindow();
		void renderJobNode(const ProgressEntrySnapshot &snapshot, const std::vector<ProgressEntrySnapshot> &allJobs, int depth);
		void renderJobContextMenu(const ProgressEntrySnapshot &snapshot);
		[[nodiscard]] static const char *toStatusLabel(JobStatus status);
		[[nodiscard]] static const char *toPriorityLabel(JobPriority priority);

	private:
		bool m_ShowProgressMonitor = true;
		bool m_ShowTaskMonitor = true;
		bool m_ShowSettingsWindow = true;
		bool m_ReserveUrgentWorker = true;
		bool m_SettingsInitialized = false;
		int m_WorkerThreadCount = 5;
		char m_DummyJobName[64] = "DummyJob";
		int m_DummyJobSteps = 5;
		int m_DummyJobDelayMs = 150;
		JobPriority m_DummyJobPriority = JobPriority::Normal;
	};
} // namespace DefectStudio
