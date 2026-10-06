#include "Core/dspch.hpp"

#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError TargetUnavailableError(const std::string &windowId)
		{
			return StructuredError{
				ErrorCategory::Runtime,
				Severity::Error,
				"The scene window is no longer available.",
				"Scene-object undo could not resolve window '" + windowId + "'.",
				"Reopen the scene window and try again.",
				"SceneObjectsSnapshotCommand",
				"scene_objects.undo_target_unavailable"};
		}

		template <typename T>
		void KeepExistingSelection(std::vector<SceneObjectId> &selection, const std::vector<T> &objects)
		{
			std::erase_if(selection, [&objects](const SceneObjectId id) {
				return FindAnnotation(objects, id) == nullptr;
			});
		}

		class SceneObjectsSnapshotCommand final : public ICommand
		{
		public:
			SceneObjectsSnapshotCommand(
				SceneObjectsWindowResolver resolveWindow,
				std::string windowId,
				SceneObjectsSnapshot before,
				SceneObjectsRestoredCallback onRestored,
				std::string description)
				: m_ResolveWindow(std::move(resolveWindow)),
				  m_WindowId(std::move(windowId)),
				  m_Before(std::move(before)),
				  m_OnRestored(std::move(onRestored)),
				  m_Description(std::move(description))
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				return {};
			}

			Result<void> Undo(CommandContext &) override
			{
				RendererWindowState *window = m_ResolveWindow(m_WindowId);
				if (window == nullptr)
					return TargetUnavailableError(m_WindowId);

				m_After = CaptureSceneObjectsSnapshot(*window);
				RestoreSceneObjectsSnapshot(*window, m_Before);
				if (m_OnRestored)
					m_OnRestored(*window);
				return {};
			}

			Result<void> Redo(CommandContext &) override
			{
				RendererWindowState *window = m_ResolveWindow(m_WindowId);
				if (window == nullptr)
					return TargetUnavailableError(m_WindowId);
				if (!m_After.has_value())
					return TargetUnavailableError(m_WindowId);

				RestoreSceneObjectsSnapshot(*window, *m_After);
				if (m_OnRestored)
					m_OnRestored(*window);
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return m_Description;
			}

			[[nodiscard]] bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			SceneObjectsWindowResolver m_ResolveWindow;
			std::string m_WindowId;
			SceneObjectsSnapshot m_Before;
			std::optional<SceneObjectsSnapshot> m_After;
			SceneObjectsRestoredCallback m_OnRestored;
			std::string m_Description;
		};
	} // namespace

	SceneObjectsSnapshot CaptureSceneObjectsSnapshot(const RendererWindowState &window)
	{
		return {
			window.pinnedMeasurements, window.freeLabels, window.sceneOrbitals,
			window.scenePlanes, window.paths ? window.paths->Store() : PathStore{}, window.showDefectFrame};
	}

	void RestoreSceneObjectsSnapshot(RendererWindowState &window, SceneObjectsSnapshot snapshot)
	{
		window.pinnedMeasurements = std::move(snapshot.pinnedMeasurements);
		window.freeLabels = std::move(snapshot.freeLabels);
		window.sceneOrbitals = std::move(snapshot.sceneOrbitals);
		window.scenePlanes = std::move(snapshot.scenePlanes);
		window.showDefectFrame = snapshot.showDefectFrame;
		if (!window.showDefectFrame) window.defectFrameSelected = false;
		SceneSystem::EnsurePathSystem(window).ReplaceStore(std::move(snapshot.paths));
		KeepExistingSelection(window.selectedPinnedMeasurements, window.pinnedMeasurements);
		KeepExistingSelection(window.selectedFreeLabels, window.freeLabels);
		KeepExistingSelection(window.selectedSceneOrbitals, window.sceneOrbitals);
		KeepExistingSelection(window.selectedScenePlanes, window.scenePlanes);
		std::erase_if(window.selectedScenePaths, [&window](const SceneObjectId id) {
			return window.paths == nullptr || !window.paths->Store().Contains(id);
		});
		window.modalTransform.reset();
		window.modalTransformSelection = {};
		window.modalTransformSceneObjectsBefore.reset();
		window.modalTransformStartedFromHandle = false;
		window.gizmoDragActive = false;
		window.pinnedMeasurementDragging = false;
		window.freeLabelDragging = false;
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		SceneSystem::SyncLabelSelection(window.sceneRegistry, window);
	}

	Unique<ICommand> CreateSceneObjectsSnapshotCommand(
		SceneObjectsWindowResolver resolveWindow,
		std::string windowId,
		SceneObjectsSnapshot before,
		SceneObjectsRestoredCallback onRestored,
		std::string description)
	{
		return CreateUnique<SceneObjectsSnapshotCommand>(
			std::move(resolveWindow),
			std::move(windowId),
			std::move(before),
			std::move(onRestored),
			std::move(description));
	}
} // namespace DefectStudio
