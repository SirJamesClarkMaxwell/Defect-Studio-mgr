#pragma once

#include <vector>

#include "Renderer/Path/PathCommands.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// The PathEditContext bound to one live window: edits apply to it and their undo entries go on
	// the shared scene-objects stack. PathCommands takes the sink as a parameter so the op layer
	// stays testable with a local UndoStack; this is the one concrete binding the UI uses, in one
	// place rather than rebuilt at every call site.
	[[nodiscard]] PathEditContext MakeWindowPathEditContext(RendererWindowState &windowState);

	// The same context with no undo sink - "apply the edit, record no history". Used by the
	// multi-path operations below, which push one snapshot for the whole batch instead of letting
	// each AddScenePath push its own and leaving a five-path paste needing five undos.
	[[nodiscard]] PathEditContext MakeSilentPathEditContext(RendererWindowState &windowState);

	// Clipboard shared across windows, same lifetime and shape as the arrow/orbital/plane ones.
	[[nodiscard]] std::vector<ScenePath> &GetScenePathClipboard();

	void CopyScenePathsToClipboard(const RendererWindowState &windowState);
	void DuplicateSelectedScenePaths(RendererWindowState &windowState);
	void PasteScenePathsFromClipboard(RendererWindowState &windowState);
	void EraseScenePaths(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);

	// Offsets every node and every cubic handle, drops the bindings and clears the persist key -
	// what a copy of a path has to have before it is inserted next to the original. A duplicate
	// that kept its bindings would be re-resolved straight back on top of the original on the next
	// frame, which looks like the duplicate silently failing.
	void OffsetAndDetachScenePath(ScenePath &path, const glm::vec3 &offset);
} // namespace DefectStudio
