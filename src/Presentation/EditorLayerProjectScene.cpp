#include "Core/dspch.hpp"

#include "Presentation/EditorLayer.hpp"

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
#include "IO/ProjectManifestIO.hpp"
#include "IO/ProjectRootsIO.hpp"
#include "IO/RecentProjectsIO.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "Renderer/ProjectSceneWindow.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	void EditorLayer::SetStartupProjectDirectory(std::optional<Path> directory)
	{
		m_StartupProjectDirectory = std::move(directory);
	}

	void EditorLayer::loadInitialProjectState()
	{
		if (m_StartupProjectDirectory.has_value())
		{
			ProjectManifest manifest;
			std::string error;
			std::error_code pathError;
			FilePath resolved = FileSystem::Absolute(m_StartupProjectDirectory->Native(), pathError);
			if (!pathError)
				resolved = FileSystem::WeaklyCanonical(resolved, pathError);
			const Path directory = Path::FromResolved(std::move(resolved));
			if (!pathError && ProjectManifestIO::Load(directory, manifest, error))
			{
				m_ActiveProject = std::move(manifest);
				m_ActiveProjectDirectory = directory;
				loadSceneObjectsForProject();
				refreshProjectDependentPanels();
				touchAndSaveRecentProject(m_ActiveProjectDirectory);
				return;
			}
			if (pathError)
				error = pathError.message();
			DS_LOG_WARN("EditorLayer: startup project at '{}' failed to load: {}",
				m_StartupProjectDirectory->String(), error);
			if (m_EventBus != nullptr)
				m_EventBus->Queue(NotificationRequestedEvent{ToNotification(StructuredError{
					ErrorCategory::IO, Severity::Warning, "Startup project failed to load",
					m_StartupProjectDirectory->String() + ": " + error,
					"Check the project directory and manifest.yaml; startup will use the recent-project fallback.",
					"EditorLayer", "project.startup.load_failed"})});
		}

		std::vector<RecentProjectEntry> recents;
		std::string recentsError;
		(void)RecentProjectsIO::Load(RecentProjectsIO::DefaultFilePath(), recents, recentsError);

		bool opened = false;
		if (!recents.empty())
		{
			ProjectManifest manifest;
			std::string manifestError;
			if (ProjectManifestIO::Load(recents.front().projectDirectory, manifest, manifestError))
			{
				m_ActiveProject = std::move(manifest);
				m_ActiveProjectDirectory = recents.front().projectDirectory;
				loadSceneObjectsForProject();
				opened = true;
			}
			else
			{
				DS_LOG_WARN("EditorLayer: most recent project at '{}' failed to load: {}",
					recents.front().projectDirectory.String(), manifestError);
			}
		}

		if (!opened)
		{
			std::string error;
			if (!ProjectRootsIO::Load(ProjectRootsIO::DefaultFilePath(), m_AdHocRoots, error))
			{
				DS_LOG_WARN("EditorLayer: failed to load project_roots.yaml: {}", error);
				m_AdHocRoots.clear();
			}
			else if (!error.empty())
			{
				// Load() still returns true after a successful migration even if persisting the
				// migrated seed failed (session stays usable) - surface that failure here instead
				// of silently dropping it.
				DS_LOG_WARN("EditorLayer: {}", error);
			}
		}

		refreshProjectDependentPanels();
	}

	void EditorLayer::loadSceneObjectsForProject()
	{
		m_KeptSceneObjects = {};
		m_AppliedSceneObjectWindows.clear();
		if (!m_ActiveProject.has_value())
			return;
		std::vector<StructuredError> warnings;
		std::string error;
		const bool loaded = SceneObjectsIO::Load(m_ActiveProjectDirectory, m_KeptSceneObjects, warnings, error);
		if (!loaded)
		{
			m_KeptSceneObjects = {};
			warnings.emplace_back(ErrorCategory::IO, Severity::Warning, "Scene object load failed", error,
				"Repair scene_objects.yaml before saving the project.", "EditorLayer", "scene_objects.load_failed");
		}
		if (auto rendererLayer = m_RendererLayer.lock())
		{
			ResetProjectSceneWindow(*rendererLayer, m_KeptSceneObjects.projectObjects, warnings);
			if (loaded)
				for (RendererWindowState &window : rendererLayer->GetWindows())
					applySceneObjectsToWindow(window);
		}
		for (const StructuredError &warning : warnings)
		{
			DS_LOG_WARN("EditorLayer: {}", warning.technicalDetails);
			if (m_EventBus != nullptr)
				m_EventBus->Queue(NotificationRequestedEvent{ToNotification(warning)});
		}
	}
} // namespace DefectStudio
