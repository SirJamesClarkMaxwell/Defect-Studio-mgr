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
	void EditorLayer::onSceneObjectsModified(const ProjectEvents::SceneObjectsModified &event)
	{
		// Scene objects only persist inside a project, so outside one there is nothing to mark dirty.
		if (m_ActiveProject.has_value() && !event.structureId.is_nil())
		{
			if (auto domainLayer = m_DomainLayer.lock())
			{
				domainLayer->Workspace().Structures().MarkModified(event.structureId);
			}
		}
	}


	void EditorLayer::applySceneObjectsToWindow(RendererWindowState &windowState)
	{
		if (!m_ActiveProject.has_value() || windowState.structureId.is_nil() || m_AppliedSceneObjectWindows.contains(windowState.windowId))
			return;
		auto domainLayer = m_DomainLayer.lock();
		if (domainLayer == nullptr)
			return;
		const auto record = domainLayer->Workspace().Structures().FindMutable(windowState.structureId).lock();
		if (record == nullptr)
			return;
		const std::string key = SceneObjectsIO::MakeStructureKey(m_ActiveProjectDirectory, record->sourcePath);
		for (PersistedStructureSceneObjects &entry : m_KeptSceneObjects.structures)
		{
			if (entry.structureKey != key || (entry.vacancies.empty() && !entry.defectFrame))
				continue;
			const bool restoreVacancies = record->structure.vacancies.empty() && !entry.vacancies.empty();
			const bool restoreFrame = !record->structure.defectFrame && entry.defectFrame;
			if (restoreVacancies || restoreFrame)
			{
				if (restoreVacancies)
					for (const PersistedVacancy &site : entry.vacancies)
						record->structure.vacancies.push_back({site.position, site.fractional,
							site.sourceSpecies, site.label, site.index, site.color, site.hidden});
				if (restoreFrame)
				{
					const PersistedDefectFrame &saved = *entry.defectFrame;
					record->structure.defectFrame = DefectFrame{saved.origin, saved.x, saved.y, saved.z};
					windowState.defectFrameAxisLength = saved.axisLength.value_or(windowState.defectFrameAxisLength);
					windowState.defectFrameAxisWidth = saved.axisWidth.value_or(windowState.defectFrameAxisWidth);
					windowState.defectFrameNegativeAxes = saved.negativeAxes.value_or(windowState.defectFrameNegativeAxes);
					windowState.showDefectFrame = saved.shown.value_or(windowState.showDefectFrame);
				}
				if (auto renderer = m_RendererLayer.lock())
					for (RendererWindowState &window : renderer->GetWindows())
						if (window.structureId == record->id)
							RebuildAndSync(window, *record, m_AtomStyleTable,
								window.selectedAtomIndices, window.selectedBondIndices);
			}
			// Consume once: opening another window after removing all markers must not restore them.
			entry.vacancies.clear();
			entry.defectFrame.reset();
		}
		std::vector<PersistedSceneObject> objects;
		bool found = false;
		if (auto rendererLayer = m_RendererLayer.lock())
		{
			for (const RendererWindowState &other : rendererLayer->GetWindows())
			{
				if (other.windowId != windowState.windowId && other.structureId == windowState.structureId && m_AppliedSceneObjectWindows.contains(other.windowId))
				{
					objects = ExtractPersistedSceneObjects(other);
					found = true;
					break;
				}
			}
		}
		if (!found)
		{
			for (const PersistedStructureSceneObjects &entry : m_KeptSceneObjects.structures)
			{
				if (entry.structureKey == key)
				{
					objects = entry.objects;
					found = true;
					break;
				}
			}
		}
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(windowState, objects, warnings);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		for (const StructuredError &warning : warnings)
		{
			if (m_EventBus != nullptr)
			{
				m_EventBus->Queue(NotificationRequestedEvent{ToNotification(warning)});
			}
		}
		m_AppliedSceneObjectWindows.insert(windowState.windowId);
	}

	void EditorLayer::createNewProject(const Path &directory)
	{
		if (directory.Empty())
			return;

		if (FileSystem::Exists(ProjectManifestIO::ManifestPath(directory).Native()))
		{
			DS_LOG_WARN(
				"EditorLayer: refusing to create a new project at '{}' - manifest.yaml already exists there (use Open instead)",
				directory.String());
			return;
		}

		ProjectManifest manifest = ProjectManifestIO::CreateNew(directory.filename().String());
		std::string error;
		if (!ProjectManifestIO::Save(directory, manifest, error))
		{
			DS_LOG_WARN("EditorLayer: failed to create project at '{}': {}", directory.String(), error);
			return;
		}

		stopPhononAnimations();
		m_ActiveProject = std::move(manifest);
		m_ActiveProjectDirectory = directory;
		loadSceneObjectsForProject();
		refreshProjectDependentPanels();
		touchAndSaveRecentProject(directory);
		loadFnvCorrectionsForProject();
		loadPhononsForProject();

		if (m_ElectronicStructureSession != nullptr)
		{
			m_ElectronicStructureSession->SetBulkDirectory({});
			m_ElectronicStructureSession->SetIrrepLabelOverrides({});
		}
	}

	void EditorLayer::openProject(const Path &directory)
	{
		if (directory.Empty())
			return;

		ProjectManifest manifest;
		std::string error;
		if (!ProjectManifestIO::Load(directory, manifest, error))
		{
			DS_LOG_WARN("EditorLayer: failed to open project at '{}': {}", directory.String(), error);
			return;
		}

		stopPhononAnimations();
		m_ActiveProject = std::move(manifest);
		m_ActiveProjectDirectory = directory;
		loadSceneObjectsForProject();
		refreshProjectDependentPanels();
		touchAndSaveRecentProject(directory);
		loadFnvCorrectionsForProject();
		loadPhononsForProject();

		if (m_ElectronicStructureSession != nullptr)
		{
			m_ElectronicStructureSession->SetBulkDirectory(m_ActiveProject->bulkDirectory);
			if (!m_ActiveProject->bulkDirectory.Empty())
				m_ElectronicStructureSession->DispatchBulkLoad();
			m_ElectronicStructureSession->SetIrrepLabelOverrides(m_ActiveProject->irrepLabelOverrides);
		}
		if (auto panel = findPanel(m_DisplacementComparisonPanelId).lock())
		{
			if (auto *displacementPanel = dynamic_cast<DisplacementComparisonPanel *>(panel.get()))
				displacementPanel->SetPersistedState(
					m_ActiveProject->displacementComparisonPath, m_ActiveProject->displacementThresholdAngstrom);
		}
	}

	void EditorLayer::touchAndSaveRecentProject(const Path &directory)
	{
		std::vector<RecentProjectEntry> recents;
		std::string error;
		(void)RecentProjectsIO::Load(RecentProjectsIO::DefaultFilePath(), recents, error);
		RecentProjectsIO::Touch(recents, directory);
		if (!RecentProjectsIO::Save(RecentProjectsIO::DefaultFilePath(), recents, error))
			DS_LOG_WARN("EditorLayer: failed to persist recent_projects.yaml: {}", error);
	}

	void EditorLayer::refreshProjectDependentPanels()
	{
		if (auto panel = findPanel(m_ProjectTreePanelId).lock())
		{
			if (auto *treePanel = dynamic_cast<ProjectTreePanel *>(panel.get()))
				treePanel->SetRoots(currentRootsMutable());
		}

		if (auto panel = findPanel(m_MaterialsCollectionPanelId).lock())
		{
			if (auto *materials = dynamic_cast<MaterialsCollectionPanel *>(panel.get()))
			{
				materials->SetProjectLibraryPath(
					m_ActiveProject.has_value()
						? m_ActiveProjectDirectory / m_ActiveProject->materialsLibraryPath
						: Path{});
			}
		}

		if (auto panel = findPanel(m_StructureHubPanelId).lock())
		{
			if (auto *hub = dynamic_cast<StructureHubPanel *>(panel.get()))
			{
				hub->SetTargetDirectory(Path{});
			}
		}

		if (auto panel = findPanel(m_CalculatorConsolePanelId).lock())
		{
			if (auto *console = dynamic_cast<CalculatorConsolePanel *>(panel.get()))
			{
				const std::string projectRoot = m_ActiveProject.has_value() ? m_ActiveProjectDirectory.String() : std::string();
				std::vector<std::string> roots;
				for (const ProjectRootEntry &entry : currentRootsMutable())
					roots.push_back(entry.path.String());
				console->SetProjectContext(projectRoot, std::move(roots));
			}
		}

		if (m_PhononSession) m_PhononSession->SetProjectRoots(currentRootsMutable());

		// App-layer coordinators authorize write targets against this list; they must not reach into
		// the UI's copy, so the roots are pushed out as an event from this one funnel.
		if (m_EventBus != nullptr)
		{
			ProjectEvents::ProjectRootsChanged rootsEvent;
			for (const ProjectRootEntry &entry : currentRootsMutable())
				rootsEvent.roots.push_back(entry.path);
			m_EventBus->Publish(rootsEvent);
		}
	}

	std::vector<ProjectRootEntry> &EditorLayer::currentRootsMutable()
	{
		if (m_ActiveProject.has_value())
			return m_ActiveProject->roots;
		return m_AdHocRoots;
	}

	void EditorLayer::persistCurrentRoots()
	{
		std::string error;
		if (m_ActiveProject.has_value())
		{
			if (!ProjectManifestIO::Save(m_ActiveProjectDirectory, *m_ActiveProject, error))
				DS_LOG_WARN("EditorLayer: failed to save manifest.yaml: {}", error);
		}
		else if (!ProjectRootsIO::Save(ProjectRootsIO::DefaultFilePath(), m_AdHocRoots, error))
		{
			DS_LOG_WARN("EditorLayer: failed to persist project_roots.yaml: {}", error);
		}
	}

	void EditorLayer::onRootAddRequested(const ProjectEvents::RootAddRequested &event)
	{
		ProjectRootEntry entry;
		entry.id = ProjectRootsIO::GenerateRootId();
		entry.path = event.path;
		entry.label = event.label;
		currentRootsMutable().push_back(std::move(entry));

		refreshProjectDependentPanels();
		persistCurrentRoots();
	}

	void EditorLayer::onRootRemoveRequested(const ProjectEvents::RootRemoveRequested &event)
	{
		std::erase_if(currentRootsMutable(), [&event](const ProjectRootEntry &entry) { return entry.id == event.rootId; });

		refreshProjectDependentPanels();
		persistCurrentRoots();
	}

	void EditorLayer::onRootPathChangedRequested(const ProjectEvents::RootPathChangedRequested &event)
	{
		std::vector<ProjectRootEntry> &roots = currentRootsMutable();
		const auto it = std::find_if(
			roots.begin(), roots.end(), [&event](const ProjectRootEntry &entry) { return entry.id == event.rootId; });
		if (it == roots.end())
			return;
		it->path = event.newPath;

		refreshProjectDependentPanels();
		persistCurrentRoots();
	}

	void EditorLayer::onBulkDirectoryChangeRequested(const ProjectEvents::BulkDirectoryChangeRequested &event)
	{
		if (m_ElectronicStructureSession != nullptr)
		{
			m_ElectronicStructureSession->SetBulkDirectory(event.directory);
			m_ElectronicStructureSession->DispatchBulkLoad();
		}

		if (m_ActiveProject.has_value())
		{
			m_ActiveProject->bulkDirectory = event.directory;
			std::string error;
			if (!ProjectManifestIO::Save(m_ActiveProjectDirectory, *m_ActiveProject, error))
				DS_LOG_WARN("EditorLayer: failed to save manifest.yaml after bulk reference change: {}", error);
		}
	}

} // namespace DefectStudio
