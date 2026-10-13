#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	PhononPanelSession::PhononPanelSession() : PhononPanelSession(WeakRef<JobSystem>{}) {}

	PhononPanelSession::PhononPanelSession(WeakRef<JobSystem> jobs, Ref<EventBus> eventBus)
		: m_JobSystem(std::move(jobs)), m_EventBus(std::move(eventBus)) { m_ObservedPlZpl = m_Settings.plZpl; }

	void PhononPanelSession::Changed()
	{
		if (m_ObservedPlZpl != m_Settings.plZpl)
		{
			m_ObservedPlZpl = m_Settings.plZpl;
			++m_PlotRevision;
		}
		m_Changed = true;
	}

	void PhononPanelSession::SetPlZpl(std::string choice)
	{
		if (m_Settings.plZpl == choice) return;
		m_Settings.plZpl = std::move(choice);
		Changed();
	}

	void PhononPanelSession::FlushChanges()
	{
		if (!std::exchange(m_Changed, false)) return;
		if (m_SaveHandler) m_SaveHandler(m_Settings);
	}

	void PhononPanelSession::Update(std::vector<RendererWindowState> &windows, double deltaSeconds, bool anyViewVisible)
	{
		m_Windows = windows;
		pollJob();
		pollCcdJob();
		pollCcdAnalysisJob();
		pollDispersionJob();
		(void)PollDatasetScan();
		// A closed target must never transfer its captured positions to a different window.
		if (!m_WindowId.empty() && !targetWindow()) clearWindow();
		if (!anyViewVisible)
		{
			StopAnimation();
			if (const auto window = targetWindow()) window->get().modeArrows.reset();
			m_BindRequested = false;
			m_WindowsBeforeOpen.clear();
			return;
		}
		syncWindow(deltaSeconds);
	}
}
