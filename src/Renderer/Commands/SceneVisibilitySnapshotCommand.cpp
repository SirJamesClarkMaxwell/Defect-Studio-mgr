#include "Core/dspch.hpp"

#include "Renderer/Commands/SceneVisibilitySnapshotCommand.hpp"

#include <optional>
#include <utility>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Renderer/RendererWindowState.hpp"
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
				"Scene-visibility undo could not resolve window '" + windowId + "'.",
				"Reopen the scene window and try again.",
				"SceneVisibilitySnapshotCommand",
				"scene_visibility.undo_target_unavailable"};
		}

		[[nodiscard]] StructuredError RedoStateUnavailableError(const std::string &windowId)
		{
			return StructuredError{
				ErrorCategory::Internal,
				Severity::Error,
				"Scene visibility could not be redone.",
				"No post-edit visibility snapshot was captured for window '" + windowId + "'.",
				"Undo the visibility change before trying to redo it.",
				"SceneVisibilitySnapshotCommand",
				"scene_visibility.redo_state_unavailable"};
		}

		class SceneVisibilitySnapshotCommand final : public ICommand
		{
		public:
			SceneVisibilitySnapshotCommand(
				SceneVisibilityWindowResolver resolveWindow,
				std::string windowId,
				HiddenSceneState before,
				std::string description)
				: m_ResolveWindow(std::move(resolveWindow)),
				  m_WindowId(std::move(windowId)),
				  m_Before(std::move(before)),
				  m_Description(std::move(description))
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				return {};
			}

			Result<void> Undo(CommandContext &) override
			{
				const std::optional<std::reference_wrapper<RendererWindowState>> window = m_ResolveWindow(m_WindowId);
				if (!window.has_value())
					return TargetUnavailableError(m_WindowId);

				m_After = CaptureHiddenSceneState(window->get().structure);
				RestoreSceneVisibilitySnapshot(window->get(), m_Before);
				return {};
			}

			Result<void> Redo(CommandContext &) override
			{
				const std::optional<std::reference_wrapper<RendererWindowState>> window = m_ResolveWindow(m_WindowId);
				if (!window.has_value())
					return TargetUnavailableError(m_WindowId);
				if (!m_After.has_value())
					return RedoStateUnavailableError(m_WindowId);

				RestoreSceneVisibilitySnapshot(window->get(), *m_After);
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
			SceneVisibilityWindowResolver m_ResolveWindow;
			std::string m_WindowId;
			HiddenSceneState m_Before;
			std::optional<HiddenSceneState> m_After;
			std::string m_Description;
		};
	} // namespace

	void RestoreSceneVisibilitySnapshot(RendererWindowState &window, const HiddenSceneState &snapshot)
	{
		const std::vector<std::size_t> selectedAtoms = window.selectedAtomIndices;
		const std::vector<std::size_t> selectedBonds = window.selectedBondIndices;
		const std::vector<std::size_t> hiddenBonds =
			ResolveHiddenBondIndices(window.structure, snapshot.bondEndpoints);
		SceneSystem::ApplySelectionAndVisibilityToScene(
			window.sceneRegistry, selectedAtoms, snapshot.atomIndices, selectedBonds, hiddenBonds);
		SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);
		window.selectedAtomIndices = selectedAtoms;
		window.selectedBondIndices = selectedBonds;
	}

	Unique<ICommand> CreateSceneVisibilitySnapshotCommand(
		SceneVisibilityWindowResolver resolveWindow,
		std::string windowId,
		HiddenSceneState before,
		std::string description)
	{
		return CreateUnique<SceneVisibilitySnapshotCommand>(
			std::move(resolveWindow), std::move(windowId), std::move(before), std::move(description));
	}
} // namespace DefectStudio
