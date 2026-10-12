#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/PhononAnalysisIO.hpp"
#include "Renderer/RendererLayer.hpp"
#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"

namespace DefectStudio
{
	PhononModesPanel::PhononModesPanel(RendererLayer &rendererLayer, WeakRef<JobSystem> jobSystem,
		Ref<EventBus> eventBus, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_RendererLayer(rendererLayer),
		  m_JobSystem(std::move(jobSystem)), m_EventBus(std::move(eventBus)) {}

	Ref<IPanel> PhononModesPanel::Clone() const
	{
		auto clone = CreateRef<PhononModesPanel>(m_RendererLayer, m_JobSystem, m_EventBus, GetTitle(), IsVisible());
		clone->m_ProjectDirectory = m_ProjectDirectory;
		clone->m_SaveHandler = m_SaveHandler;
		clone->m_Session->Settings() = m_Session->Settings();
		if (m_Session->Result())
			clone->m_Session->SetResult(*m_Session->Result());
		return clone;
	}

	std::optional<std::reference_wrapper<RendererWindowState>> PhononModesPanel::targetWindow()
	{
		if (!m_WindowId.empty())
			for (auto &window : m_RendererLayer.GetWindows())
				if (window.windowId == m_WindowId)
					return window;
		return std::nullopt;
	}

	void PhononModesPanel::StopAnimation()
	{
		if (const auto window = targetWindow())
			m_Session->StopAnimation(window->get());
		else if (m_Session->RestPositionsCaptured())
		{
			// A closed window no longer has positions to restore; forget only its captured frame.
			RendererWindowState closed;
			m_Session->StopAnimation(closed);
		}
		m_Paused = false;
	}

	void PhononModesPanel::clearWindow()
	{
		StopAnimation();
		if (const auto window = targetWindow())
			window->get().modeArrows.reset();
		m_WindowId.clear();
		m_BindRequested = false;
		m_WindowError.clear();
	}

	void PhononModesPanel::cancelJob()
	{
		if (m_PendingJob)
			if (const auto jobs = m_JobSystem.lock())
				(void)jobs->RequestCancel(m_PendingJobId);
		m_PendingJob.reset();
		m_PendingJobId = 0;
	}

	void PhononModesPanel::SetPersistedState(PhononPanelSettings settings, Path projectDirectory)
	{
		clearWindow();
		cancelJob();
		m_Session->ClearResult();
		m_Session->Settings() = std::move(settings);
		m_ProjectDirectory = std::move(projectDirectory);
		m_Error.clear();
		if (!m_Session->Settings().resultDirectory.Empty())
			loadResult(m_Session->Settings().resultDirectory);
	}

	void PhononModesPanel::persist()
	{
		if (m_SaveHandler)
			m_SaveHandler(m_Session->Settings());
	}

	void PhononModesPanel::SetDirectory(const std::string &role, const Path &directory)
	{
		if (role != "fc" && role != "gs" && role != "es" && role != "triplet" && role != "result") return;
		cancelJob();
		auto &settings = m_Session->Settings();
		if (role == "fc")
		{
			clearWindow();
			settings.forceConstantDirectory = directory;
			if (settings.resultDirectory.Empty() && !m_ProjectDirectory.Empty())
				settings.resultDirectory = m_ProjectDirectory.Join("phonons") / directory.parent_path().filename();
		}
		else if (role == "gs") settings.groundStateDirectory = directory;
		else if (role == "es") settings.excitedStateDirectory = directory;
		else if (role == "triplet") settings.tripletDirectory = directory;
		else if (role == "result")
		{
			loadResult(directory);
		}
		else return;
		SetVisible(true);
		persist();
	}

	void PhononModesPanel::loadResult(const Path &directory)
	{
		PhononAnalysisResult result;
		if (!PhononAnalysisIO::Load(directory, result, m_Error))
			return;
		StopAnimation();
		m_Session->SetResult(std::move(result));
		m_Session->Settings().resultDirectory = directory;
		const auto &loaded = *m_Session->Result();
		auto &zpl = m_Session->Settings().plZpl;
		const bool available = zpl == "computed" ? !loaded.plComputed.empty() :
			zpl == "spin_purified" ? !loaded.plSpinPurified.empty() : !loaded.plExperimental.empty();
		if (!available)
			zpl = !loaded.plExperimental.empty() ? "experimental" : !loaded.plSpinPurified.empty() ? "spin_purified" : "computed";
		m_Error.clear();
	}

	void PhononModesPanel::submitComputation()
	{
		const auto jobs = m_JobSystem.lock();
		if (!jobs)
		{
			m_Error = "JobSystem niedostępny.";
			return;
		}
		const auto &settings = m_Session->Settings();
		PhononAnalysisRequest request;
		request.forceConstantDirectory = settings.forceConstantDirectory;
		request.groundStateDirectory = settings.groundStateDirectory;
		request.excitedStateDirectory = settings.excitedStateDirectory;
		if (!settings.tripletDirectory.Empty()) request.tripletDirectory = settings.tripletDirectory;
		request.experimentalZplEv = settings.experimentalZplEv;
		request.sigmaMeV = settings.sigmaMeV;
		request.gammaMeV = settings.gammaMeV;
		request.outputDirectory = settings.resultDirectory;
		m_PendingJob = CreateRef<PhononAnalysisJob>(std::move(request));
		m_PendingJobId = jobs->Submit(m_PendingJob, JobPriority::Normal);
		m_Error.clear();
		persist();
	}

	void PhononModesPanel::pollJob()
	{
		if (!m_PendingJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_PendingJob->GetResult())
		{
			loadResult(m_PendingJob->GetResult()->outputDirectory);
			persist();
		}
		else
			m_Error = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie fononów przerwane.";
		m_PendingJob.reset();
		m_PendingJobId = 0;
	}

	// task/104f: the modes belong to the cell in the result directory (an embedded supercell is larger
	// than the FC cell); results written before 104f have no POSCAR there, so fall back to the FC dir.
	Path PhononModesPanel::supercellPath() const
	{
		const PhononPanelSettings &settings = m_Session->Settings();
		if (!settings.resultDirectory.Empty() && FileSystem::Exists(settings.resultDirectory.Join("POSCAR").Native()))
			return settings.resultDirectory.Join("POSCAR");
		return settings.forceConstantDirectory.Empty() ? Path{} : settings.forceConstantDirectory.Join("POSCAR");
	}

	void PhononModesPanel::openSupercellWindow()
	{
		clearWindow();
		if (!m_EventBus || supercellPath().Empty()) return;
		m_WindowsBeforeOpen.clear();
		for (const auto &window : m_RendererLayer.GetWindows()) m_WindowsBeforeOpen.push_back(window.windowId);
		RendererEvents::Windows::OpenStructureRequested event;
		event.filePath = supercellPath();
		event.displayName = "Superkomórka (fonony)";
		m_BindRequested = true;
		m_EventBus->Queue(event);
	}

	void PhononModesPanel::syncWindow()
	{
		const Path source = supercellPath();
		if (m_WindowId.empty() && m_BindRequested)
			for (auto &window : m_RendererLayer.GetWindows())
				if (window.structure.sourcePath == source &&
					std::find(m_WindowsBeforeOpen.begin(), m_WindowsBeforeOpen.end(), window.windowId) == m_WindowsBeforeOpen.end())
				{
					m_WindowId = window.windowId;
					m_BindRequested = false;
					break;
				}
		const auto window = targetWindow();
		if (!window)
		{
			StopAnimation();
			if (!m_WindowId.empty())
			{
				m_WindowId.clear();
				m_BindRequested = false;
			}
			m_WindowError = m_BindRequested ? "Oczekiwanie na otwarcie POSCAR..." : "Otwórz superkomórkę FC.";
			return;
		}
		auto &target = window->get();
		if (target.structure.sourcePath != source)
		{
			clearWindow();
			return;
		}
		if (!m_Session->ApplyArrows(target, m_WindowError))
			StopAnimation();
		else if (m_Session->IsAnimating() && !m_Paused)
			m_Session->AdvanceAnimation(target, ImGui::GetIO().DeltaTime);
		else if (m_Session->IsAnimating())
			m_Session->SetPhase(target, m_Session->Phase());
	}

	void PhononModesPanel::selectMode(int index)
	{
		const bool running = m_Session->IsAnimating();
		const bool paused = m_Paused;
		StopAnimation();
		m_Session->SelectMode(index);
		if (const auto window = targetWindow(); window && running)
			m_Session->StartAnimation(window->get());
		m_Paused = paused;
		persist();
	}

	void PhononModesPanel::Render()
	{
		pollJob();
		if (!IsVisible())
		{
			StopAnimation();
			if (const auto window = targetWindow()) window->get().modeArrows.reset();
			m_BindRequested = false;
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(950.0f, 760.0f), ImGuiCond_FirstUseEver);
		bool open = true;
		const bool draw = ImGui::Begin(GetTitle().c_str(), &open);
		if (!open)
		{
			SetVisible(false);
			StopAnimation();
			if (const auto window = targetWindow()) window->get().modeArrows.reset();
			m_BindRequested = false;
		}
		else if (draw)
		{
			drawInputs();
			if (m_Session->Result())
			{
				drawSummary();
				if (ImGui::BeginTable("##phononLayout", 2, ImGuiTableFlags_Resizable))
				{
					ImGui::TableSetupColumn("Mody", ImGuiTableColumnFlags_WidthStretch, 0.45f);
					ImGui::TableSetupColumn("Wykresy", ImGuiTableColumnFlags_WidthStretch, 0.55f);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); drawModesTable();
					ImGui::TableSetColumnIndex(1); drawPlots();
					ImGui::EndTable();
				}
				drawVisualisation();
			}
		}
		if (open) syncWindow();
		ImGui::End();
	}
}
