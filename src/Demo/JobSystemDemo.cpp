#include "Core/dspch.hpp"

#include <imgui.h>

#include "Core/Utils/Time.hpp"
#include "Core/JobSystem/JobContext.hpp"

#include "Demo/JobSystemDemo.hpp"

namespace DefectStudio::Demo
{

	class DemoSleepJob final : public DefectStudio::IJob
	{
	public:
		DemoSleepJob(
			std::string name,
			int steps,
			int delayMs,
			Ref<std::atomic_bool> pauseRequested)
			: m_Name(std::move(name)),
			  m_Steps((steps <= 0) ? 1 : steps),
			  m_DelayMs((delayMs <= 0) ? 1 : delayMs),
			  m_PauseRequested(std::move(pauseRequested))
		{
		}

		[[nodiscard]] std::string GetName() const override { return m_Name; }

		[[nodiscard]] std::string GetType() const override { return "DemoSleepJob"; }

		void Execute(DefectStudio::JobContext &context) override
		{
			context.SetStage("running");

			for (int step = 1; step <= m_Steps; ++step)
			{
				context.ThrowIfCancellationRequested();

				// Cooperative pause: jobs wait while pause flag is set.
				while (m_PauseRequested && m_PauseRequested->load(std::memory_order_relaxed))
				{
					context.ThrowIfCancellationRequested();
					context.SetStage("paused");
					std::this_thread::sleep_for(Time::Milliseconds(10));
				}

				context.SetStage("running");
				std::this_thread::sleep_for(Time::Milliseconds(m_DelayMs));
				context.SetProgress(static_cast<float>(step), static_cast<float>(m_Steps));
				context.SetMessage("step " + std::to_string(step));
			}

			context.SetStage("done");
		}

	private:
		std::string m_Name;
		int m_Steps = 1;
		int m_DelayMs = 1;
		Ref<std::atomic_bool> m_PauseRequested;
	};

	class DemoFailingJob final : public DefectStudio::IJob
	{
	public:
		explicit DemoFailingJob(std::string message)
			: m_Message(std::move(message))
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return "DemoFailingJob";
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "DemoFailingJob";
		}

		void Execute(DefectStudio::JobContext &context) override
		{
			context.LogError(m_Message);
			throw std::runtime_error(m_Message);
		}

	private:
		std::string m_Message;
	};

	const char *ToStatusLabel(DefectStudio::JobStatus status)
	{
		switch (status)
		{
		case DefectStudio::JobStatus::Queued:
			return "Queued";
		case DefectStudio::JobStatus::Running:
			return "Running";
		case DefectStudio::JobStatus::Completed:
			return "Completed";
		case DefectStudio::JobStatus::Failed:
			return "Failed";
		case DefectStudio::JobStatus::Cancelled:
			return "Cancelled";
		default:
			return "Unknown";
		}
	}

	bool IsFinished(DefectStudio::JobStatus status)
	{
		return status == DefectStudio::JobStatus::Completed
			|| status == DefectStudio::JobStatus::Failed
			|| status == DefectStudio::JobStatus::Cancelled;
	}


	JobSystemDemo::JobSystemDemo()
		: m_DemoEventBus(CreateRef<EventBus>()),
		  m_DemoJobSystem(CreateUnique<JobSystem>(CreateWeakRef(m_DemoEventBus), 0)),
		  m_PauseRequested(CreateRef<std::atomic_bool>(false))
	{
		tryLoadPreferredFont();
	}

	JobSystemDemo::~JobSystemDemo()
	{
		if (m_DemoJobSystem)
			m_DemoJobSystem->Shutdown();
	}

	void JobSystemDemo::Render()
	{
		if (!m_DemoJobSystem)
			return;

		syncProgressTracker();

		ImGui::Begin("JobSystem Demo");
		if (m_DemoFont != nullptr)
			ImGui::PushFont(m_DemoFont);

		ImGui::TextUnformatted("API workflow:");
		ImGui::BulletText("Submit(const Ref<IJob> &, priority)");
		ImGui::BulletText("RequestCancel(jobId)");
		ImGui::BulletText("GetAllJobs/GetJob/GetLogs");

		renderControls();
		renderProgressPanel();
		renderJobsPanel();
		renderSelectedJobPanel();

		if (m_DemoFont != nullptr)
			ImGui::PopFont();
		ImGui::End();
	}

	void JobSystemDemo::submitSleepJob(int steps, int delayMs, JobPriority priority)
	{
		++m_JobCounter;
		const std::string name = "demo-sleep-" + std::to_string(m_JobCounter);
		auto job = CreateRef<DemoSleepJob>(name, steps, delayMs, m_PauseRequested);
		const JobId id = m_DemoJobSystem->Submit(job, priority);
		if (id == 0)
			return;

		m_KnownJobs.push_back(id);
		m_SelectedJobId = id;
		m_ProgressIds[id] = m_ProgressTracker.Register(name, static_cast<std::size_t>(std::max(1, steps)));
	}

	void JobSystemDemo::submitFailingJob()
	{
		const JobId id = m_DemoJobSystem->Submit(CreateRef<DemoFailingJob>("Intentional demo failure"), JobPriority::High);
		if (id == 0)
			return;

		m_KnownJobs.push_back(id);
		m_SelectedJobId = id;
		m_ProgressIds[id] = m_ProgressTracker.Register("demo-failing", 1);
	}

	void JobSystemDemo::submitBulkJobs(int count, int steps, int delayMs, JobPriority priority)
	{
		const int safeCount = std::max(1, count);
		for (int i = 0; i < safeCount; ++i)
			submitSleepJob(steps, delayMs, priority);
	}

	void JobSystemDemo::syncProgressTracker()
	{
		for (const JobId id : m_KnownJobs)
		{
			auto snapshot = m_DemoJobSystem->GetJob(id);
			if (!snapshot.has_value())
				continue;

			auto progressIt = m_ProgressIds.find(id);
			if (progressIt == m_ProgressIds.end())
				continue;

			const auto progressId = progressIt->second;
			const std::size_t completed = static_cast<std::size_t>(std::max(0.0f, snapshot->completedWork));
			m_ProgressTracker.Report(progressId, completed);

			if (IsFinished(snapshot->status))
				m_ProgressTracker.Finish(progressId);
		}
	}

	void JobSystemDemo::renderControls()
	{
		if (!ImGui::CollapsingHeader("Controls", m_ShowControls ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
			return;

		ImGui::SetNextItemWidth(500.0f);
		ImGui::InputInt("Steps", &m_NewJobSteps);
		ImGui::SetNextItemWidth(500.0f);
		ImGui::InputInt("Delay (ms)", &m_NewJobDelayMs);
		ImGui::SetNextItemWidth(500.0f);
		ImGui::InputInt("Bulk Count", &m_BulkJobCount);

		if (ImGui::Button("Add Job"))
			submitSleepJob(m_NewJobSteps, m_NewJobDelayMs, JobPriority::Normal);

		ImGui::SameLine();
		if (ImGui::Button("Add Many Jobs"))
			submitBulkJobs(m_BulkJobCount, m_NewJobSteps, m_NewJobDelayMs, JobPriority::Normal);

		ImGui::SameLine();
		if (ImGui::Button("Add Failing Job"))
			submitFailingJob();

		ImGui::SameLine();
		if (ImGui::Button(m_PauseRequested->load(std::memory_order_relaxed) ? "Resume Jobs" : "Pause Jobs"))
		{
			const bool pause = !m_PauseRequested->load(std::memory_order_relaxed);
			m_PauseRequested->store(pause, std::memory_order_relaxed);
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel Selected") && m_SelectedJobId != 0)
			(void)m_DemoJobSystem->RequestCancel(m_SelectedJobId);
	}

	void JobSystemDemo::renderProgressPanel()
	{
		if (!ImGui::CollapsingHeader("ProgressTracker (green)", m_ShowProgress ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
			return;

		const auto snapshots = m_ProgressTracker.Snapshot();
		if (snapshots.empty())
		{
			ImGui::TextUnformatted("No tracked progress yet.");
			return;
		}

		float totalCompleted = 0.0f;
		float totalWork = 0.0f;
		for (const auto &entry : snapshots)
		{
			totalCompleted += static_cast<float>(entry.completedWork);
			totalWork += static_cast<float>(entry.totalWork);
		}

		const float summaryRatio = (totalWork <= 0.0f) ? 0.0f : std::clamp(totalCompleted / totalWork, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.18f, 0.72f, 0.24f, 1.0f));
		ImGui::ProgressBar(summaryRatio, ImVec2(-1.0f, 0.0f), "Total progress");
		ImGui::PopStyleColor();

		for (const auto &entry : snapshots)
		{
			const float ratio = (entry.totalWork == 0)
				? 0.0f
				: std::clamp(static_cast<float>(entry.completedWork) / static_cast<float>(entry.totalWork), 0.0f, 1.0f);
			ImGui::Text("#%llu %s", static_cast<unsigned long long>(entry.id), entry.label.c_str());
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.20f, 0.78f, 0.28f, 1.0f));
			ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f));
			ImGui::PopStyleColor();
		}
	}

	void JobSystemDemo::renderJobsPanel()
	{
		const auto allJobs = m_DemoJobSystem->GetAllJobs();
		std::vector<JobSnapshot> waitingJobs;
		std::vector<JobSnapshot> finishedJobs;

		for (const auto &snapshot : allJobs)
		{
			if (IsFinished(snapshot.status))
				finishedJobs.push_back(snapshot);
			else
				waitingJobs.push_back(snapshot);
		}

		ImGui::SeparatorText("Jobs");
		if (ImGui::CollapsingHeader("Waiting / Running", m_ShowWaiting ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
		{
			for (const auto &snapshot : waitingJobs)
			{
				char label[256]{};
				std::snprintf(label, sizeof(label), "#%llu | %s | %s", static_cast<unsigned long long>(snapshot.id), snapshot.name.c_str(), ToStatusLabel(snapshot.status));
				if (ImGui::Selectable(label, m_SelectedJobId == snapshot.id))
					m_SelectedJobId = snapshot.id;
			}
		}

		if (ImGui::CollapsingHeader("Finished", m_ShowFinished ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
		{
			for (const auto &snapshot : finishedJobs)
			{
				char label[256]{};
				std::snprintf(label, sizeof(label), "#%llu | %s | %s", static_cast<unsigned long long>(snapshot.id), snapshot.name.c_str(), ToStatusLabel(snapshot.status));
				if (ImGui::Selectable(label, m_SelectedJobId == snapshot.id))
					m_SelectedJobId = snapshot.id;
			}
		}
	}

	void JobSystemDemo::renderSelectedJobPanel()
	{
		if (!ImGui::CollapsingHeader("Selected Job", m_ShowSelectedJob ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
			return;

		if (m_SelectedJobId == 0)
		{
			ImGui::TextUnformatted("No selected job.");
			return;
		}

		auto selected = m_DemoJobSystem->GetJob(m_SelectedJobId);
		if (!selected.has_value())
		{
			ImGui::TextUnformatted("Selected job no longer exists.");
			return;
		}

		ImGui::Text("id: %llu", static_cast<unsigned long long>(selected->id));
		ImGui::Text("name: %s", selected->name.c_str());
		ImGui::Text("type: %s", selected->type.c_str());
		ImGui::Text("status: %s", ToStatusLabel(selected->status));
		ImGui::Text("stage: %s", selected->currentStage.c_str());
		ImGui::Text("message: %s", selected->currentMessage.c_str());
		ImGui::Text("progress: %.1f / %.1f", selected->completedWork, selected->totalWork);

		if (!selected->errorMessage.empty())
			ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "error: %s", selected->errorMessage.c_str());

		if (ImGui::CollapsingHeader("Logs", m_ShowLogs ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
		{
			const auto logs = m_DemoJobSystem->GetLogs(selected->id);
			for (const auto &log : logs)
				ImGui::Text("[%d] %s", static_cast<int>(log.level), log.message.c_str());
		}
	}

	void JobSystemDemo::tryLoadPreferredFont()
	{
		ImGuiIO &io = ImGui::GetIO();

		const std::array<const char *, 3> candidates = {
			"C:/Windows/Fonts/CascadiaCode.ttf",
			"C:/Windows/Fonts/CascadiaMono.ttf",
			"C:/Windows/Fonts/segoeui.ttf"
		};

		for (const char *path : candidates)
		{
			if (!std::filesystem::exists(path))
				continue;

			m_DemoFont = io.Fonts->AddFontFromFileTTF(path, 16.0f);
			if (m_DemoFont != nullptr)
			{
				// Use VSCode-like typography globally after successful font load.
				io.FontDefault = m_DemoFont;
				break;
			}
		}
	}
} // namespace DefectStudio::Demo
