#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/PhononAnalysisIO.hpp"
#include "IO/CcdAnalysisIO.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"
#include "ScientificRuntime/Python/CcdInputsJob.hpp"
#include "ScientificRuntime/Python/CcdAnalysisJob.hpp"
#include "ScientificRuntime/Python/PhononDispersionJob.hpp"

namespace DefectStudio
{
	std::optional<std::reference_wrapper<RendererWindowState>> PhononPanelSession::targetWindow()
	{
		if (m_Windows && !m_WindowId.empty())
			for (auto &window : m_Windows->get())
				if (window.windowId == m_WindowId)
					return window;
		return std::nullopt;
	}

	void PhononPanelSession::StopAnimation()
	{
		if (const auto window = targetWindow())
			StopAnimation(window->get());
		else if (RestPositionsCaptured())
		{
			// A closed window no longer has positions to restore; forget only its captured frame.
			RendererWindowState closed;
			StopAnimation(closed);
		}
		m_Paused = false;
	}

	void PhononPanelSession::clearWindow()
	{
		StopAnimation();
		if (const auto window = targetWindow())
			window->get().modeArrows.reset();
		m_WindowId.clear();
		m_WindowsBeforeOpen.clear();
		m_BindRequested = false;
		m_WindowError.clear();
	}

	Path PhononPanelSession::supercellPath() const
	{
		if (m_Settings.animationSource == "bulk_q")
			return m_BulkMode && !m_Settings.selectedBulkModeDirectory.Empty() ? m_Settings.selectedBulkModeDirectory / "POSCAR" : Path{};
		if (m_Settings.animationSource == "ccd_direction")
			return m_CcdResult && m_CcdResult->displacement ? Path::FromUtf8(m_CcdResult->displacement->gsSource) : Path{};
		const PhononPanelSettings &settings = Settings();
		if (!settings.resultDirectory.Empty() && FileSystem::Exists(settings.resultDirectory.Join("POSCAR").Native()))
			return settings.resultDirectory.Join("POSCAR");
		return settings.forceConstantDirectory.Empty() ? Path{} : settings.forceConstantDirectory.Join("POSCAR");
	}

	void PhononPanelSession::openSupercellWindow()
	{
		clearWindow();
		if (!m_Windows || !m_EventBus || supercellPath().Empty()) return;
		m_WindowsBeforeOpen.clear();
		for (const auto &window : m_Windows->get()) m_WindowsBeforeOpen.push_back(window.windowId);
		RendererEvents::Windows::OpenStructureRequested event;
		event.filePath = supercellPath();
		event.displayName = CaseWindowTitle();
		m_BindRequested = true;
		m_EventBus->Queue(event);
	}

	void PhononPanelSession::syncWindow(double deltaSeconds)
	{
		if (IsAnimating() && m_AnimationSource != m_Settings.animationSource) StopAnimation();
		const Path source = supercellPath();
		auto &caseWindows = m_Settings.animationSource == "bulk_q" ? m_BulkWindows : m_CaseWindows;
		if (m_WindowId.empty() && !m_BindRequested)
			if (const auto bound = caseWindows.find(ActiveCaseId()); bound != caseWindows.end())
				for (auto &window : m_Windows->get())
					if (window.windowId == bound->second && CanBindWindow(window, source, m_ProjectDirectory))
					{
						m_WindowId = window.windowId;
						break;
					}
		if (m_WindowId.empty())
			for (auto &window : m_Windows->get())
				if (CanBindWindow(window, source, m_ProjectDirectory) &&
					(!m_BindRequested || std::find(m_WindowsBeforeOpen.begin(), m_WindowsBeforeOpen.end(), window.windowId) == m_WindowsBeforeOpen.end()))
				{
					m_WindowId = window.windowId;
					if (!ActiveCaseId().empty()) caseWindows[ActiveCaseId()] = m_WindowId;
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
			m_WindowError = m_BindRequested ? (m_Settings.animationSource == "ccd_direction" ?
				"Oczekiwanie na otwarcie geometrii GS..." : "Oczekiwanie na otwarcie POSCAR...") :
				m_Settings.animationSource == "ccd_direction" ? "Otwórz geometrię GS." : "Otwórz superkomórkę FC.";
			if (m_Settings.animationSource == "bulk_q") m_WindowError = m_BindRequested ? "Oczekiwanie na otwarcie bulku..." : "Otwórz superkomórkę bulku.";
			return;
		}
		auto &target = window->get();
		if (!CanBindWindow(target, source, m_ProjectDirectory))
		{
			clearWindow();
			return;
		}
		if (!WindowMatches(target, m_WindowError))
			StopAnimation();
		else if (IsAnimating() && !m_Paused)
			AdvanceAnimation(target, deltaSeconds);
		else if (IsAnimating())
			SetPhase(target, Phase());
		else
			(void)ApplyArrows(target, m_WindowError);
	}
}
