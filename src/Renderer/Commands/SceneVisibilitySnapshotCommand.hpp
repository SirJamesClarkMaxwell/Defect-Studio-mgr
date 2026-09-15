#pragma once

#include <functional>
#include <optional>
#include <string>

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"
#include "Renderer/Scene/HiddenSceneState.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	using SceneVisibilityWindowResolver =
		std::function<std::optional<std::reference_wrapper<RendererWindowState>>(const std::string &windowId)>;

	// Restores only atom/bond visibility. The live atom and bond selections are preserved.
	void RestoreSceneVisibilitySnapshot(RendererWindowState &window, const HiddenSceneState &snapshot);

	// Pushed with UndoStack::PushExecuted after a visibility edit. Execute is a no-op; Undo captures
	// the current hidden state for redo, then restores `before`. The window is resolved by id on every
	// Undo/Redo so a closed target reports a StructuredError instead of retaining a stale pointer.
	[[nodiscard]] Unique<ICommand> CreateSceneVisibilitySnapshotCommand(
		SceneVisibilityWindowResolver resolveWindow,
		std::string windowId,
		HiddenSceneState before,
		std::string description = "Change scene visibility");
} // namespace DefectStudio
