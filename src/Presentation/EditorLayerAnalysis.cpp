#include "Core/dspch.hpp"

#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/CalculationSummaryPanel.hpp"
#include "Presentation/Panels/DefectEnergeticsPanel.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"
#include "Core/Logging/Logger.hpp"

#include <array>
#include "Events/ProjectEvents.hpp"
#include "IO/EnergeticsIO.hpp"
#include "IO/FnvCorrectionsIO.hpp"
#include "IO/PhononPanelSettingsIO.hpp"

namespace DefectStudio
{
	void EditorLayer::stopPhononAnimations()
	{
		for (const auto &entry : m_Panels.Entries())
			if (const auto panel = std::dynamic_pointer_cast<PhononModesPanel>(entry.panel))
				panel->StopAnimation();
	}

	void EditorLayer::loadPhononsForProject()
	{
		PhononPanelSettings settings;
		if (m_ActiveProject)
		{
			std::vector<std::string> warnings;
			std::string error;
			if (!PhononPanelSettingsIO::Load(m_ActiveProjectDirectory, settings, warnings, error))
			{
				DS_LOG_WARN("EditorLayer: failed to load phonon_panel.yaml: {}", error);
				settings = {};
			}
			for (const auto &warning : warnings) DS_LOG_WARN("EditorLayer: phonon_panel.yaml: {}", warning);
		}
		for (const auto &entry : m_Panels.Entries())
			if (const auto panel = std::dynamic_pointer_cast<PhononModesPanel>(entry.panel))
			{
				panel->SetPersistedState(settings, m_ActiveProject ? m_ActiveProjectDirectory : Path{});
				panel->SetSaveHandler([this](const PhononPanelSettings &state) {
					if (!m_ActiveProject) return;
					std::string error;
					if (!PhononPanelSettingsIO::Save(m_ActiveProjectDirectory, state, error))
						DS_LOG_WARN("EditorLayer: failed to save phonon_panel.yaml: {}", error);
				});
			}
	}

	void EditorLayer::onPhononDirectoryRequested(const ProjectEvents::PhononDirectoryRequested &event)
	{
		if (const auto panel = std::dynamic_pointer_cast<PhononModesPanel>(findPanel(m_PhononModesPanelId).lock()))
			panel->SetDirectory(event.role, event.directory);
	}

	void EditorLayer::registerCalculationSummaryViews()
	{
		m_CalculationSummarySession = CreateRef<CalculationSummarySession>(m_JobSystem);
		m_CalculationSummaryPanelId = registerPanel<CalculationSummaryPanel>(
			m_CalculationSummarySession, "Calculation Summary", false);
		// The first instance keeps the existing INI/visibility key for startup cache restoration.
		m_CalculationSummarySession->SetUpdateInstancesHandler([this] {
			// This runs before EditorLayer iterates the registry to render panels.
			for (const PanelId id : m_Panels.GetIds())
			{
				const auto panel = std::dynamic_pointer_cast<CalculationSummaryPanel>(findPanel(id).lock());
				if (!panel)
					continue;
				if (panel->IsClosed())
				{
					(void)m_Panels.Remove(id);
					if (id == m_CalculationSummaryPanelId)
						m_CalculationSummaryPanelId = 0;
				}
				else if (panel->Session() != m_CalculationSummarySession)
					panel->Session()->Update();
			}
		});
	}

	void EditorLayer::registerDefectEnergeticsViews()
	{
		m_DefectEnergeticsSession = CreateRef<DefectEnergeticsSession>(m_JobSystem);
		using View = DefectEnergeticsPanel::View;
		// Preserve the old INI/visibility key for the conditions view.
		m_DefectEnergeticsPanelId = registerPanel<DefectEnergeticsPanel>(
			m_DefectEnergeticsSession, View::Conditions, "Defect Energetics", false);
		const std::array views{m_DefectEnergeticsPanelId,
			registerPanel<DefectEnergeticsPanel>(m_DefectEnergeticsSession, View::Formation, "Energie tworzenia", false),
			registerPanel<DefectEnergeticsPanel>(m_DefectEnergeticsSession, View::Transitions, "Poziomy przejść", false),
			registerPanel<DefectEnergeticsPanel>(m_DefectEnergeticsSession, View::Concentrations, "Koncentracje", false),
			m_FnvCorrectionPanelId};
		m_DefectEnergeticsSession->SetShowViewsHandler([this, views](const bool visible) {
			for (const auto id : views)
				if (const auto panel = findPanel(id).lock(); panel && (visible || id != m_FnvCorrectionPanelId))
					panel->SetVisible(visible); // FNV Correction stays: it is used on its own too
		});
		const auto fnv = std::dynamic_pointer_cast<FnvCorrectionPanel>(findPanel(m_FnvCorrectionPanelId).lock());
		m_DefectEnergeticsSession->SetFnvPanel(fnv);
		// Explicit composition-root bridge: the session does not silently write another panel's state.
		m_DefectEnergeticsSession->SetFnvStoreHandler([panel = WeakRef<FnvCorrectionPanel>(fnv)](PersistedFnvCorrection entry) {
			if (const auto target = panel.lock())
				target->StoreCorrection(std::move(entry));
		});
		m_CalculationSummarySession->SetBandEdgesProvider(
			[session = WeakRef<DefectEnergeticsSession>(m_DefectEnergeticsSession)]() -> std::optional<VaspRunRecordIO::BandEdges> {
				const auto energetics = session.lock();
				return energetics ? energetics->BulkBandEdges() : std::nullopt;
			});
	}

	void EditorLayer::loadFnvCorrectionsForProject()
	{
		auto panel = std::dynamic_pointer_cast<FnvCorrectionPanel>(findPanel(m_FnvCorrectionPanelId).lock());
		if (panel == nullptr)
			return;
		FnvCorrectionsFile file;
		if (m_ActiveProject.has_value())
		{
			std::vector<std::string> warnings;
			std::string error;
			if (!FnvCorrectionsIO::Load(m_ActiveProjectDirectory, file, warnings, error))
			{
				DS_LOG_WARN("EditorLayer: failed to load fnv_corrections.yaml: {}", error);
				file = {};
			}
			for (const auto &warning : warnings)
				DS_LOG_WARN("EditorLayer: fnv_corrections.yaml: {}", warning);
		}
		panel->SetPersistedState(std::move(file));
		// Without a project the corrections live for the session only, as before.
		panel->SetSaveHandler([this](const FnvCorrectionsFile &corrections) {
			if (m_DefectEnergeticsSession)
				m_DefectEnergeticsSession->InvalidateInputs();
			if (!m_ActiveProject.has_value())
				return;
			std::string error;
			if (!FnvCorrectionsIO::Save(m_ActiveProjectDirectory, corrections, error))
				DS_LOG_WARN("EditorLayer: failed to save fnv_corrections.yaml: {}", error);
		});
		loadEnergeticsForProject();
	}

	void EditorLayer::loadEnergeticsForProject()
	{
		const auto &session = m_DefectEnergeticsSession;
		if (session == nullptr)
			return;
		session->SetFnvPanel(std::dynamic_pointer_cast<FnvCorrectionPanel>(findPanel(m_FnvCorrectionPanelId).lock()));
		EnergeticsFile file;
		if (m_ActiveProject.has_value())
		{
			std::vector<std::string> warnings;
			std::string error;
			if (!EnergeticsIO::Load(m_ActiveProjectDirectory, file, warnings, error))
			{
				DS_LOG_WARN("EditorLayer: failed to load energetics.yaml: {}", error);
				file = {};
			}
			for (const auto &warning : warnings)
				DS_LOG_WARN("EditorLayer: energetics.yaml: {}", warning);
		}
		session->SetPersistedState(std::move(file));
		session->SetSaveHandler([this](const EnergeticsFile &energetics) {
			if (!m_ActiveProject.has_value())
				return;
			std::string error;
			if (!EnergeticsIO::Save(m_ActiveProjectDirectory, energetics, error))
				DS_LOG_WARN("EditorLayer: failed to save energetics.yaml: {}", error);
		});
	}

	void EditorLayer::onCalculationSummaryOpenRequested(const ProjectEvents::CalculationSummaryOpenRequested &event)
	{
		if (event.directory.Empty())
			return;
		Ref<CalculationSummaryPanel> mostRecent;
		for (const auto &entry : m_Panels.Entries())
		{
			const auto panel = std::dynamic_pointer_cast<CalculationSummaryPanel>(entry.panel);
			if (!panel || panel->IsClosed())
				continue;
			if (panel->Session()->Directory() == event.directory)
			{
				panel->RequestFocus();
				return;
			}
			if (!mostRecent || panel->FocusOrder() > mostRecent->FocusOrder())
				mostRecent = panel;
		}
		auto panel = std::dynamic_pointer_cast<CalculationSummaryPanel>(findPanel(m_CalculationSummaryPanelId).lock());
		if (!panel || panel->IsClosed() || !panel->Session()->Directory().Empty())
		{
			const auto session = CreateRef<CalculationSummarySession>(m_JobSystem, false);
			session->SetBandEdgesProvider([this]() -> std::optional<VaspRunRecordIO::BandEdges> {
				return m_DefectEnergeticsSession ? m_DefectEnergeticsSession->BulkBandEdges() : std::nullopt;
			});
			const auto id = registerPanel<CalculationSummaryPanel>(session, "Calculation Summary", true);
			panel = std::dynamic_pointer_cast<CalculationSummaryPanel>(findPanel(id).lock());
			panel->SetTitle("Calculation Summary: " + event.directory.filename().Utf8() + "##" + std::to_string(id));
			if (mostRecent)
				panel->DockAsTab(*mostRecent);
		}
		panel->Session()->OpenDirectory(event.directory);
		panel->RequestFocus();
	}

	void EditorLayer::onEnergeticsFoldersAddRequested(const ProjectEvents::EnergeticsFoldersAddRequested &event)
	{
		if (!m_DefectEnergeticsSession)
			return;
		for (const Path &directory : event.directories)
			m_DefectEnergeticsSession->AddDefectFolder(directory);
		if (const auto panel = findPanel(m_DefectEnergeticsPanelId).lock())
			panel->SetVisible(true);
	}
} // namespace DefectStudio
