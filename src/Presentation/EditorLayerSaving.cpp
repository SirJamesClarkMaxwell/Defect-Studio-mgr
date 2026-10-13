#include "Core/dspch.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Commands/CommandService.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Domain/DomainLayer.hpp"
#include "Events/EditorUiEvents.hpp"
#include "Events/ProjectEvents.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/PoscarWriter.hpp"
#include "IO/POTCARWriter.hpp"
#include "IO/ProjectManifestIO.hpp"
#include "IO/EnergeticsIO.hpp"
#include "IO/FnvCorrectionsIO.hpp"
#include "IO/ProjectRootsIO.hpp"
#include "IO/RecentProjectsIO.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "IO/TextFileIO.hpp"
#include "Presentation/ProjectSceneSave.hpp"
#include "Renderer/ProjectSceneWindow.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/BondSettingsPanel.hpp"
#include "Presentation/Panels/NewStructureWizardPanel.hpp"
#include "Presentation/Panels/CalculatorConsolePanel.hpp"
#include "Presentation/Panels/ElectronicStructurePanel.hpp"
#include "Presentation/Panels/ElectronicStructureSession.hpp"
#include "Presentation/Panels/SceneDensityLoader.hpp"
#include "Presentation/Panels/ExportImagePanel.hpp"
#include "Presentation/Panels/OccupationDiagramPanel.hpp"
#include "Presentation/Panels/DisplacementComparisonPanel.hpp"
#include "Presentation/Panels/GroupTheoryPanel.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"
#include "Presentation/Panels/DefectEnergeticsSession.hpp"
#include "Presentation/Panels/CalculationSummarySession.hpp"
#include "Presentation/Panels/ElementCatalogPanel.hpp"
#include "Presentation/Panels/ObjectPropertiesPanel.hpp"
#include "Presentation/Panels/RendererPanel.hpp"
#include "Presentation/Panels/SceneOutlinerPanel.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"
#include "Presentation/Panels/MaterialsCollectionPanel.hpp"
#include "Presentation/Panels/StructureCreationTabsPanel.hpp"
#include "Presentation/Panels/StructureHubPanel.hpp"
#include "Presentation/Panels/TerminalPanel.hpp"
#include "Presentation/Panels/TextEditorPanel.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"

namespace DefectStudio
{
	void EditorLayer::onProjectSaveRequested(const CoreEvents::ProjectSaveRequested &)
	{
		stopPhononAnimations();
		if (!m_ActiveProject.has_value())
		{
			persistCurrentRoots();
			if (m_EventBus != nullptr)
			{
				m_EventBus->Queue(NotificationRequestedEvent{ToNotification(StructuredError{
					ErrorCategory::Validation,
					Severity::Warning,
					"No project open - scene objects (labels, arrows) were not saved. Open or create a project.",
					"Project save was requested without an active project; only project roots were persisted.",
					"Open or create a project before saving scene objects.",
					"EditorLayer",
					"project.save.no_project"})});
			}
			return;
		}
		pollPendingWindowRestores();
		SceneObjectsFile sceneObjects = m_KeptSceneObjects;
		if (auto rendererLayer = m_RendererLayer.lock())
			sceneObjects.projectObjects = GatherProjectSceneObjects(*rendererLayer);
		std::vector<StructureId> savedStructures;
		if (auto domainLayer = m_DomainLayer.lock())
		{
			for (const auto &record : domainLayer->Workspace().Structures().Records())
			{
				if (record->sourcePath.Empty())
					continue;
				const std::string key = SceneObjectsIO::MakeStructureKey(m_ActiveProjectDirectory, record->sourcePath);
				auto entry = std::find_if(sceneObjects.structures.begin(), sceneObjects.structures.end(),
					[&](const auto &candidate) { return candidate.structureKey == key; });
				// A registered but unopened structure can still have an unconsumed sidecar.
				if (entry != sceneObjects.structures.end() &&
					((!entry->vacancies.empty() && record->structure.vacancies.empty()) ||
						(entry->defectFrame && !record->structure.defectFrame)))
					continue;
				if (entry == sceneObjects.structures.end())
				{
					if (record->structure.vacancies.empty() && !record->structure.defectFrame)
						continue;
					sceneObjects.structures.push_back({key, {}, {}});
					entry = std::prev(sceneObjects.structures.end());
				}
				entry->vacancies.clear();
				for (const VacancySite &site : record->structure.vacancies)
					entry->vacancies.push_back(
						{site.position, site.fractional, site.sourceSpecies, site.label, site.index, site.color, site.hidden});
				entry->defectFrame.reset();
				if (const auto &frame = record->structure.defectFrame)
				{
					PersistedDefectFrame saved{frame->origin, frame->x, frame->y, frame->z};
					// ponytail: the first window showing the structure speaks for all of them.
					if (auto rendererLayer = m_RendererLayer.lock())
						for (const RendererWindowState &window : rendererLayer->GetWindows())
							if (window.structureId == record->id)
							{
								saved.axisLength = window.defectFrameAxisLength;
								saved.axisWidth = window.defectFrameAxisWidth;
								saved.negativeAxes = window.defectFrameNegativeAxes;
								saved.shown = window.showDefectFrame;
								break;
							}
					entry->defectFrame = saved;
				}
				savedStructures.push_back(record->id);
			}
			if (auto rendererLayer = m_RendererLayer.lock())
			{
				std::unordered_map<std::string, std::vector<std::pair<std::string, std::vector<PersistedSceneObject>>>>
					grouped;
				std::unordered_map<std::string, StructureId> ids;
				for (RendererWindowState &window : rendererLayer->GetWindows())
				{
					if (window.structureId.is_nil())
						continue;
					const auto record = domainLayer->Workspace().Structures().Find(window.structureId).lock();
					if (record == nullptr)
						continue;
					const std::string key =
						SceneObjectsIO::MakeStructureKey(m_ActiveProjectDirectory, record->sourcePath);
					grouped[key].push_back({window.windowId, ExtractPersistedSceneObjects(window)});
					ids[key] = window.structureId;
				}
				for (auto &[key, windows] : grouped)
				{
					const std::string &active = rendererLayer->GetLastFocusedViewportWindowId();
					std::vector<std::vector<PersistedSceneObject>> ordered;
					for (const auto &window : windows)
						if (window.first != active)
							ordered.push_back(window.second);
					for (const auto &window : windows)
						if (window.first == active)
							ordered.push_back(window.second);
					const auto merged = MergeWindowSceneObjects(ordered);
					auto entry = std::find_if(sceneObjects.structures.begin(), sceneObjects.structures.end(),
											  [&](const auto &candidate) { return candidate.structureKey == key; });
					if (entry == sceneObjects.structures.end())
						sceneObjects.structures.push_back({key, merged});
					else
						entry->objects = merged;
					savedStructures.push_back(ids[key]);
				}
			}
		}
		std::string error;
		auto domainLayer = m_DomainLayer.lock();
		std::vector<StructuredError> warnings;
		const StructureFileWriter writeStructureFile = [](const CrystalStructure &structure, const Path &path) {
			return PoscarWriter::Write(structure, path, true);
		};
		if (domainLayer == nullptr ||
			!SaveProjectWithSceneObjects(m_ActiveProjectDirectory, *m_ActiveProject, sceneObjects,
										 domainLayer->Workspace().Structures(), savedStructures, writeStructureFile, warnings, error))
		{
			DS_LOG_WARN("EditorLayer: project scene save failed: {}", error);
			if (m_EventBus != nullptr)
				m_EventBus->Queue(NotificationRequestedEvent{
					ToNotification(StructuredError{ErrorCategory::IO, Severity::Warning, "Project scene save failed",
												   error, "Fix the project path or permissions and try again.",
													   "EditorLayer", "scene_objects.save_failed"})});
			for (const StructuredError &warning : warnings)
				if (m_EventBus != nullptr)
					m_EventBus->Queue(NotificationRequestedEvent{ToNotification(warning)});
			return;
		}
		for (const StructuredError &warning : warnings)
			if (m_EventBus != nullptr)
				m_EventBus->Queue(NotificationRequestedEvent{ToNotification(warning)});
		m_KeptSceneObjects = std::move(sceneObjects);
		for (auto &entry : m_KeptSceneObjects.structures)
			for (const auto &id : savedStructures)
				if (const auto record = domainLayer->Workspace().Structures().Find(id).lock();
					record != nullptr && entry.structureKey == SceneObjectsIO::MakeStructureKey(m_ActiveProjectDirectory, record->sourcePath))
				{
					entry.vacancies.clear();
					entry.defectFrame.reset();
					break;
				}
		if (auto rendererLayer = m_RendererLayer.lock())
			for (RendererWindowState &window : rendererLayer->GetWindows())
				if (window.isProjectScene)
					window.sceneObjectsDirty = false;
		// Legacy in-app-built structure export removed in Step 11. All new structures flow
		// through StructureLifecycleCoordinator (AddStructureToProjectRequested event).
	}

	void EditorLayer::onProjectSaveAsRequested(const CoreEvents::ProjectSaveAsRequested &)
	{
		stopPhononAnimations();
		Path suggestedDir = m_ActiveProjectDirectory.empty()
			? Path::FromResolved(FileSystem::CurrentPath())
			: m_ActiveProjectDirectory.parent_path();

		auto dialogResult = Platform::PickFolder(suggestedDir);
		if (!dialogResult.HasValue())
		{
			DS_LOG_WARN("EditorLayer: save-as dialog failed: {}", dialogResult.Error().suggestion);
			return;
		}

		const std::optional<Path> &picked = dialogResult.Value();
		if (!picked.has_value())
			return; // User cancelled

		const Path selectedDir = *picked;
		const std::string projectName = m_ActiveProjectDirectory.empty()
			? "NewProject"
			: m_ActiveProjectDirectory.filename().String();

		const Path newProjectDir = selectedDir / projectName;

		if (!FileSystem::Exists(newProjectDir.Native()))
			FileSystem::CreateDirectories(newProjectDir.Native());

		createNewProject(newProjectDir);
		onProjectSaveRequested(CoreEvents::ProjectSaveRequested{});
	}

	void EditorLayer::onStructureFileSaveRequested(const CoreEvents::StructureFileSaveRequested &)
	{
		stopPhononAnimations();
		if (auto panel = findPanel(m_StructureHubPanelId).lock())
			panel->SetVisible(true);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(NotificationRequestedEvent{ToNotification(StructuredError(
				ErrorCategory::Validation, Severity::Warning,
				"Aby zapisać nową strukturę, wybierz folder i użyj „Add to Project” w Structure Hub.",
				"Structure file save requires the creation-session add-to-project flow",
				"Zmiany struktur należących do projektu zapiszesz skrótem Ctrl+S.", "EditorLayer"))});
	}

	// The only POTCAR trigger left after Step 11 removed the legacy save path: a structure that was
	// created with "Export POTCAR" ticked, the moment it becomes a project member.
	void EditorLayer::onProjectStructureAdded(const DomainEvents::ProjectStructureAdded &event)
	{
		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		if (domainLayer == nullptr)
			return;
		Ref<const StructureRecord> record =
			domainLayer->Workspace().Structures().Find(event.newStructureId).lock();
		if (record == nullptr || !record->exportPotcar)
			return;

		exportPotcarNextToPoscar(*record, Path::FromResolved(event.poscarPath.parent_path()));
	}

	// POTCAR is written with its bare VASP name next to the POSCAR, so an input directory holding one
	// structure is ready to run. Failures are surfaced as error notifications rather than log lines -
	// a missing pseudopotential is the user's to fix, and a silent skip looks like a successful export.
	void EditorLayer::exportPotcarNextToPoscar(const StructureRecord &structureRecord, const Path &projectDir)
	{
		const auto reportFailure = [this, &structureRecord](const StructuredError &error) {
			DS_LOG_WARN("POTCAR write failed for '{}': {}", structureRecord.displayName, error.userMessage);
			if (m_EventBus == nullptr)
				return;
			Notification notification = ToNotification(error);
			notification.title = "POTCAR export failed";
			notification.source = "EditorLayer";
			notification.pinned = true;
			m_EventBus->Queue(NotificationRequestedEvent{std::move(notification)});
		};

		if (m_CurrentConfig == nullptr || m_CurrentConfig->ui.pseudopotentialDir.Empty())
		{
			reportFailure(StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				"POTCAR export failed: no pseudopotential directory configured",
				"structure: " + structureRecord.displayName,
				"Set ui.pseudopotential_dir to your VASP pseudopotential directory"));
			return;
		}

		auto potcarResult = POTCARWriter::Write(
			structureRecord.structure, projectDir / "POTCAR", m_CurrentConfig->ui.pseudopotentialDir);
		if (!potcarResult.HasValue())
			reportFailure(potcarResult.Error());
	}

} // namespace DefectStudio
