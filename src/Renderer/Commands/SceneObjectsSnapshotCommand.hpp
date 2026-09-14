#pragma once

#include <functional>
#include <string>

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// Pins, free labels and scene arrows of one window, copied whole (few objects per window).
	using SceneObjectsSnapshot = RendererWindowState::LabelUndoSnapshot;
	using SceneObjectsWindowResolver = std::function<RendererWindowState *(const std::string &windowId)>;
	using SceneObjectsRestoredCallback = std::function<void(RendererWindowState &window)>;

	[[nodiscard]] SceneObjectsSnapshot CaptureSceneObjectsSnapshot(const RendererWindowState &window);

	// Replaces the window's pins/free labels/arrows with `snapshot`, clears their selections, ends any
	// label/arrow drag or quick edit in progress and resyncs the label entities.
	void RestoreSceneObjectsSnapshot(RendererWindowState &window, SceneObjectsSnapshot snapshot);

	// One undo entry on the app-global UndoStack for one scene-object edit. Pushed with
	// UndoStack::PushExecuted right BEFORE the edit is applied (call sites snapshot first, then mutate),
	// so Execute does nothing. Undo captures the window's current objects as the redo state, then
	// restores `before`; Redo restores that captured state. The window is looked up by id on every
	// Undo/Redo: when it is gone, both return a StructuredError with code
	// "scene_objects.undo_target_unavailable" and change nothing, so the stack index does not move.
	// `onRestored` runs after every successful restore. IsUndoable() is true.
	[[nodiscard]] Unique<ICommand> CreateSceneObjectsSnapshotCommand(
		SceneObjectsWindowResolver resolveWindow,
		std::string windowId,
		SceneObjectsSnapshot before,
		SceneObjectsRestoredCallback onRestored = {},
		std::string description = "Edit scene objects");
} // namespace DefectStudio
