#include "Core/dspch.hpp"

#include <imgui.h>

#include <chrono>
#include <algorithm>
#include <sstream>

#include "App/Application.hpp"
#include "Core/JobSystem/TestJobs/TestJobs.hpp"
#include "Presentation/EditorLayer.hpp"

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	EditorLayer::EditorLayer() : Layer("EditorLayer")
	{
	}

	void EditorLayer::OnAttach()
	{
		DS_LOG_INFO("EditorLayer attached");
	}

	void EditorLayer::OnDetach()
	{
		DS_LOG_INFO("EditorLayer detached");
	}

	void EditorLayer::OnImGuiRender()
	{
		renderMainMenuBar();
		renderProgressMonitorWindow();
		renderTaskMonitorWindow();
		renderSettingsWindow();
	}

	void EditorLayer::renderMainMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		if (ImGui::BeginMenu("Plik"))
		{
			ImGui::MenuItem("Nowy", nullptr, false, false);
			ImGui::MenuItem("Otworz", nullptr, false, false);
			ImGui::MenuItem("Zapisz", nullptr, false, false);
			if (ImGui::BeginMenu("Ostatnie projekty"))
			{
				ImGui::MenuItem("Brak ostatnich projektow", nullptr, false, false);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Import DFT"))
			{
				ImGui::MenuItem("VASP", nullptr, false, false);
				ImGui::MenuItem("QE", nullptr, false, false);
				ImGui::MenuItem("FHI-aims", nullptr, false, false);
				ImGui::MenuItem("CP2K", nullptr, false, false);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Eksport"))
			{
				ImGui::MenuItem("JSON", nullptr, false, false);
				ImGui::MenuItem("CSV", nullptr, false, false);
				ImGui::MenuItem("LaTeX", nullptr, false, false);
				ImGui::MenuItem("ZIP", nullptr, false, false);
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edycja"))
		{
			ImGui::MenuItem("Cofnij", "Ctrl+Z", false, false);
			ImGui::MenuItem("Ponow", "Ctrl+Y", false, false);
			if (ImGui::BeginMenu("Clipboard"))
			{
				ImGui::MenuItem("Kopiuj wezel", nullptr, false, false);
				ImGui::MenuItem("Wytnij wezel", nullptr, false, false);
				ImGui::MenuItem("Wklej wezel", nullptr, false, false);
				ImGui::MenuItem("Duplikuj wezel", nullptr, false, false);
				ImGui::EndMenu();
			}
			ImGui::MenuItem("Znajdz defekt", "Ctrl+F", false, false);
			ImGui::MenuItem("Szybka nawigacja", "Ctrl+G", false, false);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Widok"))
		{
			ImGui::MenuItem("Panel glownego monitora", nullptr, &m_ShowProgressMonitor);
			ImGui::MenuItem("Task Monitor", nullptr, &m_ShowTaskMonitor);
			ImGui::MenuItem("Settings", nullptr, &m_ShowSettingsWindow);
			if (ImGui::BeginMenu("Uklad paneli"))
			{
				ImGui::MenuItem("Preset 1", nullptr, false, false);
				ImGui::MenuItem("Preset 2", nullptr, false, false);
				ImGui::MenuItem("Preset 3", nullptr, false, false);
				ImGui::MenuItem("Preset 4", nullptr, false, false);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Styl wizualizacji"))
			{
				ImGui::MenuItem("Ball-stick", nullptr, false, false);
				ImGui::MenuItem("CPK", nullptr, false, false);
				ImGui::MenuItem("Polyhedral", nullptr, false, false);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Nakladki"))
			{
				ImGui::MenuItem("CHGCAR", nullptr, false, false);
				ImGui::MenuItem("Orbital KS", nullptr, false, false);
				ImGui::MenuItem("Spin density", nullptr, false, false);
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Defekt"))
		{
			ImGui::MenuItem("Nowy defekt wizard", "Ctrl+D", false, false);
			ImGui::MenuItem("Generuj supercele per q", nullptr, false, false);
			ImGui::MenuItem("ShakeNBreak", nullptr, false, false);
			ImGui::MenuItem("NEB", nullptr, false, false);
			ImGui::MenuItem("Diagram formacji", nullptr, false, false);
			ImGui::MenuItem("negative-U", nullptr, false, false);
			ImGui::MenuItem("ZPL", nullptr, false, false);
			ImGui::MenuItem("ZFS", nullptr, false, false);
			ImGui::MenuItem("Scorecard", nullptr, false, false);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Obliczenia"))
		{
			ImGui::MenuItem("Uruchom", "F5", false, false);
			ImGui::MenuItem("Zatrzymaj", "F7", false, false);
			if (ImGui::BeginMenu("Funkcjonały"))
			{
				ImGui::MenuItem("PBE", nullptr, false, false);
				ImGui::MenuItem("HSE06", nullptr, false, false);
				ImGui::MenuItem("PBE+U", nullptr, false, false);
				ImGui::MenuItem("vdW-DF", nullptr, false, false);
				ImGui::MenuItem("G0W0", nullptr, false, false);
				ImGui::EndMenu();
			}
			ImGui::MenuItem("Korekcja FNV/eFNV", nullptr, false, false);
			ImGui::MenuItem("Menadzer kolejki", nullptr, false, false);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Narzedzia"))
		{
			ImGui::MenuItem("Baza danych", "Ctrl+B", false, false);
			ImGui::MenuItem("Import z h-bn.info", nullptr, false, false);
			ImGui::MenuItem("QPOD API", nullptr, false, false);
			ImGui::MenuItem("Ranking SPE", nullptr, false, false);
			ImGui::MenuItem("Brouwer", nullptr, false, false);
			ImGui::MenuItem("Kalkulator Purcella", nullptr, false, false);
			ImGui::MenuItem("Preferencje", nullptr, false, false);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Pomoc"))
		{
			ImGui::MenuItem("Dokumentacja", "F1", false, false);
			ImGui::MenuItem("Tutorial", nullptr, false, false);
			ImGui::MenuItem("Lista skrotow", nullptr, false, false);
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	void EditorLayer::renderTaskMonitorWindow()
	{
		if (!m_ShowTaskMonitor)
			return;

		ImGui::SetNextWindowSize(ImVec2(460.0f, 260.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Task Monitor", &m_ShowTaskMonitor))
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted("Submit a dummy job to the global job system.");
		ImGui::InputText("Name", m_DummyJobName, IM_ARRAYSIZE(m_DummyJobName));
		ImGui::InputInt("Steps", &m_DummyJobSteps);
		ImGui::InputInt("Step delay (ms)", &m_DummyJobDelayMs);

		m_DummyJobSteps = std::max(1, m_DummyJobSteps);
		m_DummyJobDelayMs = std::max(1, m_DummyJobDelayMs);

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
		ImGui::Text("Queue/worker threads: %llu", static_cast<unsigned long long>(Application::Get().GetJobSystem().GetThreadCount()));

		ImGui::Separator();
		ImGui::TextUnformatted("This window is the general Task Monitor entry point.");
		ImGui::End();
	}

	void EditorLayer::renderSettingsWindow()
	{
		auto &jobSystem = Application::Get().GetJobSystem();
		if (!m_SettingsInitialized)
		{
			m_WorkerThreadCount = static_cast<int>(jobSystem.GetThreadCount());
			m_SettingsInitialized = true;
		}

		if (!m_ShowSettingsWindow)
			return;

		ImGui::SetNextWindowSize(ImVec2(420.0f, 190.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Settings", &m_ShowSettingsWindow))
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted("Job system settings");
		ImGui::InputInt("Worker threads", &m_WorkerThreadCount);
		m_WorkerThreadCount = std::max(1, m_WorkerThreadCount);
		ImGui::Checkbox("Reserve one urgent worker", &m_ReserveUrgentWorker);
		ImGui::Text("Effective workers: %d", m_WorkerThreadCount + (m_ReserveUrgentWorker ? 1 : 0));
		if (ImGui::Button("Apply"))
			(void)jobSystem.SetThreadCount(static_cast<std::size_t>(m_WorkerThreadCount + (m_ReserveUrgentWorker ? 1 : 0)));
		ImGui::SameLine();
		if (ImGui::Button("Reset to 5"))
		{
			m_WorkerThreadCount = 5;
			(void)jobSystem.SetThreadCount(static_cast<std::size_t>(m_WorkerThreadCount + (m_ReserveUrgentWorker ? 1 : 0)));
		}
		ImGui::Text("Current threads: %llu", static_cast<unsigned long long>(jobSystem.GetThreadCount()));
		ImGui::TextUnformatted("Default startup value stays 5 plus optional urgent reserve.");
		ImGui::End();
	}

	const char *EditorLayer::toStatusLabel(JobStatus status)
	{
		switch (status)
		{
		case JobStatus::Queued:
			return "Queued";
		case JobStatus::Running:
			return "Running";
		case JobStatus::Completed:
			return "Completed";
		case JobStatus::Failed:
			return "Failed";
		case JobStatus::Cancelled:
			return "Cancelled";
		default:
			return "Unknown";
		}
	}

	const char *EditorLayer::toPriorityLabel(JobPriority priority)
	{
		switch (priority)
		{
		case JobPriority::Lowest:
			return "Lowest";
		case JobPriority::Low:
			return "Low";
		case JobPriority::Normal:
			return "Normal";
		case JobPriority::High:
			return "High";
		case JobPriority::Highest:
			return "Highest";
		default:
			return "Normal";
		}
	}

	void EditorLayer::renderProgressMonitorWindow()
	{
		if (!m_ShowProgressMonitor)
			return;

		auto &progressTracker = Application::Get().GetProgressTracker();
		auto snapshots = progressTracker.GetAllSnapshots();

		ImGui::SetNextWindowSize(ImVec2(980.0f, 640.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Progress Monitor", &m_ShowProgressMonitor))
		{
			ImGui::End();
			return;
		}

		if (snapshots.empty())
		{
			ImGui::TextUnformatted("No jobs yet.");
			ImGui::End();
			return;
		}

		std::vector<ProgressEntrySnapshot> rootJobs;
		for (const auto &snapshot : snapshots)
		{
			if (snapshot.parentId == 0)
				rootJobs.push_back(snapshot);
		}

		std::sort(rootJobs.begin(), rootJobs.end(), [](const ProgressEntrySnapshot &left, const ProgressEntrySnapshot &right) {
			return left.id < right.id;
		});

		ImGui::Text("Jobs: %llu", static_cast<unsigned long long>(snapshots.size()));
		ImGui::Separator();

		for (const auto &snapshot : rootJobs)
			renderJobNode(snapshot, snapshots, 0);

		ImGui::End();
	}

	void EditorLayer::renderJobNode(const ProgressEntrySnapshot &snapshot, const std::vector<ProgressEntrySnapshot> &allJobs, int depth)
	{
		const float ratio = (snapshot.totalWork <= 0.0f)
			? 0.0f
			: std::clamp(snapshot.completedWork / snapshot.totalWork, 0.0f, 1.0f);

		std::ostringstream title;
		title << "#" << snapshot.id << " | " << snapshot.label << " | "
		      << static_cast<int>(ratio * 100.0f) << "% | "
		      << snapshot.currentStage << " | "
		      << (snapshot.currentMessage.empty() ? "-" : snapshot.currentMessage) << " | "
		      << toPriorityLabel(snapshot.priority) << " | " << toStatusLabel(snapshot.status);

		const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed
			| ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_OpenOnArrow
			| ImGuiTreeNodeFlags_DefaultOpen;

		if (depth > 0)
			ImGui::Indent(static_cast<float>(depth) * 18.0f);

		const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<intptr_t>(snapshot.id)), flags, "%s", title.str().c_str());
		if (ImGui::BeginPopupContextItem())
		{
			renderJobContextMenu(snapshot);
			ImGui::EndPopup();
		}

		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.18f, 0.72f, 0.24f, 1.0f));
		ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f));
		ImGui::PopStyleColor();

		if (open)
		{
			ImGui::Text("source: %s", snapshot.source.c_str());
			ImGui::Text("parent: %llu", static_cast<unsigned long long>(snapshot.parentId));
			ImGui::Text("stage: %s", snapshot.currentStage.c_str());
			ImGui::Text("message: %s", snapshot.currentMessage.c_str());
			ImGui::Text("progress: %.1f / %.1f", snapshot.completedWork, snapshot.totalWork);
			ImGui::Text("created: %lld", static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(snapshot.createdAt.time_since_epoch()).count()));
			ImGui::Text("started: %lld", static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(snapshot.startedAt.time_since_epoch()).count()));
			ImGui::Text("finished: %lld", static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(snapshot.finishedAt.time_since_epoch()).count()));
			if (!snapshot.errorSummary.empty())
				ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "error: %s", snapshot.errorSummary.c_str());

			std::vector<ProgressEntrySnapshot> childJobs;
			for (const auto &candidate : allJobs)
			{
				if (candidate.parentId == snapshot.id)
					childJobs.push_back(candidate);
			}
			std::sort(childJobs.begin(), childJobs.end(), [](const ProgressEntrySnapshot &left, const ProgressEntrySnapshot &right) {
				return left.id < right.id;
			});

			for (const auto &child : childJobs)
				renderJobNode(child, allJobs, depth + 1);

			ImGui::TreePop();
		}

		if (depth > 0)
			ImGui::Unindent(static_cast<float>(depth) * 18.0f);
	}

	void EditorLayer::renderJobContextMenu(const ProgressEntrySnapshot &snapshot)
	{
		auto &jobSystem = Application::Get().GetJobSystem();

		if (ImGui::MenuItem("Copy JobId"))
			ImGui::SetClipboardText(std::to_string(snapshot.id).c_str());
		if (ImGui::MenuItem("Copy Label"))
			ImGui::SetClipboardText(snapshot.label.c_str());
		if (ImGui::MenuItem("Copy Source"))
			ImGui::SetClipboardText(snapshot.source.c_str());

		ImGui::Separator();

		const bool canCancel = snapshot.status == JobStatus::Queued || snapshot.status == JobStatus::Running;
		if (ImGui::MenuItem("Cancel", nullptr, false, canCancel))
			(void)jobSystem.RequestCancel(snapshot.id);
		if (ImGui::MenuItem("Reset", nullptr, false, snapshot.status == JobStatus::Completed || snapshot.status == JobStatus::Failed || snapshot.status == JobStatus::Cancelled))
			(void)jobSystem.Reset(snapshot.id);
		if (ImGui::MenuItem("Retry", nullptr, false, snapshot.status == JobStatus::Completed || snapshot.status == JobStatus::Failed || snapshot.status == JobStatus::Cancelled))
			(void)jobSystem.Retry(snapshot.id);
	}
} // namespace DefectStudio
