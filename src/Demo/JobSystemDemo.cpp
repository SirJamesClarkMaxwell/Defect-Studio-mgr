#include "Core/dspch.hpp"

#include <imgui.h>

#include "App/Events/JobEvents.hpp"
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

	class DemoChainedJob final : public DefectStudio::IJob
	{
	public:
		DemoChainedJob(std::string jobName, int index)
			: m_JobName(std::move(jobName)), m_Index(index)
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return m_JobName + "[" + std::to_string(m_Index) + "]";
		}

		[[nodiscard]] std::string GetType() const override { return "DemoChainedJob"; }

		void Execute(DefectStudio::JobContext &context) override
		{
			context.SetStage("working");
			std::this_thread::sleep_for(Time::Milliseconds(100));
			context.SetProgress(1.0f, 1.0f);
			context.SetStage("done");
		}

	private:
		std::string m_JobName;
		int m_Index;
	};

	class DemoSequentialSubmitterJob final : public DefectStudio::IJob
	{
	public:
		DemoSequentialSubmitterJob(std::string jobName, int chainCount)
			: m_JobName(std::move(jobName)), m_ChainCount(chainCount)
		{
		}

		[[nodiscard]] std::string GetName() const override { return m_JobName; }

		[[nodiscard]] std::string GetType() const override { return "DemoSequentialSubmitterJob"; }

		void Execute(DefectStudio::JobContext &context) override
		{
			context.SetStage("submitting-chain");
			for (int i = 0; i < m_ChainCount; ++i)
			{
				context.ThrowIfCancellationRequested();
				auto chainedJob = CreateRef<DemoChainedJob>(m_JobName, i);
				const auto id = context.SubmitJobSequential(chainedJob, JobPriority::Normal);
				if (id == 0)
				{
					context.LogError("Failed to submit chained job " + std::to_string(i));
					return;
				}
				context.SetProgress(static_cast<float>(i + 1), static_cast<float>(m_ChainCount));
				context.SetMessage("Submitted chain job " + std::to_string(i));
			}
			context.SetStage("all-submitted");
			context.SetProgress(1.0f, 1.0f);
		}

	private:
		std::string m_JobName;
		int m_ChainCount;
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
		  m_ProgressTracker(CreateWeakRef(m_DemoEventBus)),
		  m_PauseRequested(CreateRef<std::atomic_bool>(false))
	{
		m_CurrentThreadCount = m_DemoJobSystem->GetThreadCount();
		m_ThreadCountInput = static_cast<int>(m_CurrentThreadCount);

		m_StartedSubscription = m_DemoEventBus->Subscribe<JobStartedEvent>(
			[this](const JobStartedEvent &event) {
				appendLifecycleEvent("Started #" + std::to_string(event.id) + " [" + event.name + "]");
			});

		m_CompletedSubscription = m_DemoEventBus->Subscribe<JobCompletedEvent>(
			[this](const JobCompletedEvent &event) {
				appendLifecycleEvent("Completed #" + std::to_string(event.id) + " [" + event.name + "]");
			});

		m_CancelledSubscription = m_DemoEventBus->Subscribe<JobCancelledEvent>(
			[this](const JobCancelledEvent &event) {
				appendLifecycleEvent("Cancelled #" + std::to_string(event.id) + " [" + event.name + "]");
			});

		m_FailedSubscription = m_DemoEventBus->Subscribe<JobFailedEvent>(
			[this](const JobFailedEvent &event) {
				appendLifecycleEvent("Failed #" + std::to_string(event.id) + " [" + event.name + "]: " + event.errorMessage);
			});
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

		m_DemoEventBus->ProcessQueue();

		ImGui::TextUnformatted("API workflow:");
		ImGui::BulletText("Submit(const Ref<IJob> &, priority)");
		ImGui::BulletText("SubmitAfter(const Ref<IJob> &, delay, priority)");
		ImGui::BulletText("SetThreadCount(threadCount)");
		ImGui::BulletText("RequestCancel(jobId)");
		ImGui::BulletText("GetAllJobs/GetJob/GetLogs");

		renderControls();
		renderEventBusPanel();
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
	}

	void JobSystemDemo::submitFailingJob()
	{
		const JobId id = m_DemoJobSystem->Submit(CreateRef<DemoFailingJob>("Intentional demo failure"), JobPriority::High);
		if (id == 0)
			return;

		m_KnownJobs.push_back(id);
		m_SelectedJobId = id;
	}

	void JobSystemDemo::submitBulkJobs(int count, int steps, int delayMs, JobPriority priority)
	{
		const int safeCount = std::max(1, count);
		for (int i = 0; i < safeCount; ++i)
			submitSleepJob(steps, delayMs, priority);
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
		ImGui::SetNextItemWidth(500.0f);
		ImGui::InputInt("Delay before start (ms)", &m_DelayedStartMs);
		ImGui::SetNextItemWidth(500.0f);
		ImGui::InputInt("Worker threads", &m_ThreadCountInput);
		ImGui::Text("Current workers: %llu", static_cast<unsigned long long>(m_CurrentThreadCount));

		if (ImGui::Button("Add Job"))
			submitSleepJob(m_NewJobSteps, m_NewJobDelayMs, JobPriority::Normal);

		ImGui::SameLine();
		if (ImGui::Button("Add Delayed Job"))
		{
			++m_JobCounter;
			const std::string name = "demo-delayed-" + std::to_string(m_JobCounter);
			auto job = CreateRef<DemoSleepJob>(name, m_NewJobSteps, m_NewJobDelayMs, m_PauseRequested);
			const auto delay = Time::Milliseconds(std::max(0, m_DelayedStartMs));
			const JobId id = m_DemoJobSystem->SubmitAfter(job, delay, JobPriority::Normal);
			if (id != 0)
			{
				m_KnownJobs.push_back(id);
				m_SelectedJobId = id;
			}
		}

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

		ImGui::SameLine();
		if (ImGui::Button("Reset Selected") && m_SelectedJobId != 0)
			(void)m_DemoJobSystem->Reset(m_SelectedJobId);

		ImGui::SameLine();
		if (ImGui::Button("Retry Selected") && m_SelectedJobId != 0)
			(void)m_DemoJobSystem->Retry(m_SelectedJobId);

		ImGui::SameLine();
		if (ImGui::Button("Sequential Chain"))
		{
			++m_JobCounter;
			const std::string name = "demo-sequential-" + std::to_string(m_JobCounter);
			auto job = CreateRef<DemoSequentialSubmitterJob>(name, m_BulkJobCount);
			const JobId id = m_DemoJobSystem->Submit(job, JobPriority::Normal);
			if (id != 0)
			{
				m_KnownJobs.push_back(id);
				m_SelectedJobId = id;
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Apply Worker Count"))
		{
			const auto requested = static_cast<std::size_t>(std::max(1, m_ThreadCountInput));
			if (m_DemoJobSystem->SetThreadCount(requested))
				m_CurrentThreadCount = m_DemoJobSystem->GetThreadCount();
			m_ThreadCountInput = static_cast<int>(m_CurrentThreadCount);
		}
	}

	void JobSystemDemo::renderEventBusPanel()
	{
		if (!ImGui::CollapsingHeader("EventBus Job Lifecycle", m_ShowEventBus ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
			return;

		ImGui::Text("Subscriptions: %llu", static_cast<unsigned long long>(m_DemoEventBus->GetTotalSubscriberCount()));
		ImGui::Text("Queued events: %llu", static_cast<unsigned long long>(m_DemoEventBus->GetQueuedEventCount()));

		if (m_LifecycleEvents.empty())
		{
			ImGui::TextUnformatted("No lifecycle events yet.");
			return;
		}

		for (const auto &entry : m_LifecycleEvents)
			ImGui::TextUnformatted(entry.c_str());
	}

	void JobSystemDemo::appendLifecycleEvent(std::string eventLine)
	{
		m_LifecycleEvents.push_back(std::move(eventLine));
	}

	void JobSystemDemo::renderProgressPanel()
	{
		if (!ImGui::CollapsingHeader("Job Monitor", m_ShowProgress ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
			return;

		const auto snapshots = m_ProgressTracker.GetAllSnapshots();
		if (snapshots.empty())
		{
			ImGui::TextUnformatted("No tracked progress yet.");
			return;
		}

		int runningCount = 0;
		int queuedCount = 0;
		int doneCount = 0;
		int failedCount = 0;
		float totalActiveRatio = 0.0f;
		int activeCount = 0;

		for (const auto &entry : snapshots)
		{
			switch (entry.status)
			{
			case JobStatus::Running:
				++runningCount;
				break;
			case JobStatus::Queued:
				++queuedCount;
				break;
			case JobStatus::Completed:
				++doneCount;
				break;
			case JobStatus::Failed:
				++failedCount;
				break;
			case JobStatus::Cancelled:
				break;
			}

			if (!entry.finished)
			{
				const float ratio = (entry.totalWork <= 0.0f)
					? 0.0f
					: std::clamp(entry.completedWork / entry.totalWork, 0.0f, 1.0f);
				totalActiveRatio += ratio;
				++activeCount;
			}
		}

		ImGui::Text("Session jobs: %llu", static_cast<unsigned long long>(snapshots.size()));
		ImGui::SameLine();
		ImGui::Text("Running: %d", runningCount);
		ImGui::SameLine();
		ImGui::Text("Queued: %d", queuedCount);
		ImGui::SameLine();
		ImGui::Text("Done: %d", doneCount);
		ImGui::SameLine();
		ImGui::Text("Failed: %d", failedCount);

		const float avgActiveRatio = (activeCount == 0) ? 0.0f : (totalActiveRatio / static_cast<float>(activeCount));
		ImGui::Text("Avg active progress: %.1f%%", avgActiveRatio * 100.0f);

		if (ImGui::BeginTable("job-monitor-table", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("number", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableSetupColumn("expand", ImGuiTableColumnFlags_WidthFixed, 56.0f);
			ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch, 120.0f);
			ImGui::TableSetupColumn("Progress Bar", ImGuiTableColumnFlags_WidthStretch, 180.0f);
			ImGui::TableSetupColumn("left time", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthStretch, 220.0f);
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableHeadersRow();

			for (std::size_t row = 0; row < snapshots.size(); ++row)
			{
				const auto &entry = snapshots[row];
				const float ratio = (entry.totalWork <= 0.0f)
					? 0.0f
					: std::clamp(entry.completedWork / entry.totalWork, 0.0f, 1.0f);
				const bool isExpanded = (m_ExpandedProgressId == entry.id);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%llu", static_cast<unsigned long long>(entry.id));

				ImGui::TableSetColumnIndex(1);
				if (ImGui::SmallButton((std::string(isExpanded ? "v##exp-" : ">##exp-") + std::to_string(entry.id)).c_str()))
					m_ExpandedProgressId = isExpanded ? 0 : entry.id;

				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(entry.source.c_str());

				ImGui::TableSetColumnIndex(3);
				ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), (std::to_string(static_cast<int>(ratio * 100.0f)) + "%").c_str());

				ImGui::TableSetColumnIndex(4);
				if (!entry.finished && entry.totalWork > 0.0f && entry.completedWork > 0.0f)
				{
					const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Time::Now() - entry.startedAt).count();
					const float speed = entry.completedWork / std::max(1.0f, static_cast<float>(elapsedMs));
					const float remaining = std::max(0.0f, entry.totalWork - entry.completedWork);
					const int etaMs = static_cast<int>(remaining / std::max(0.0001f, speed));
					ImGui::Text("%d s", etaMs / 1000);
				}
				else
				{
					ImGui::TextUnformatted(entry.finished ? "0 s" : "n/d");
				}

				ImGui::TableSetColumnIndex(5);
				if (!entry.currentMessage.empty())
					ImGui::TextUnformatted(entry.currentMessage.c_str());
				else if (!entry.errorSummary.empty())
					ImGui::TextUnformatted(entry.errorSummary.c_str());
				else
					ImGui::TextUnformatted("-");

				ImGui::TableSetColumnIndex(6);
				ImGui::TextUnformatted(ToStatusLabel(entry.status));

				if (!isExpanded)
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("details");
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted("-");
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("label: %s", entry.label.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("stage: %s", entry.currentStage.c_str());
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("created: %lld", static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(entry.createdAt.time_since_epoch()).count()));
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("started: %lld", static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(entry.startedAt.time_since_epoch()).count()));
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("finished: %lld", static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(entry.finishedAt.time_since_epoch()).count()));
			}

			ImGui::EndTable();
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

} // namespace DefectStudio::Demo
