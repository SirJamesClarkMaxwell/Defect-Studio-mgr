#pragma once

#include <vector>

#include "Renderer/Path/PathCommands.hpp"
#include "Renderer/Path/PathEvaluator.hpp"
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
	// Shared scene-relative sizing for free segments.
	[[nodiscard]] float GetDefaultSceneSegmentLength(const RendererWindowState &windowState);

	// Session default for newly atom-bound paths; existing nodes keep their own buffer.
	[[nodiscard]] float &GetScenePathAtomBuffer();

	// User-facing segment creation. Both select the new path and record one undo entry.
	[[nodiscard]] Result<SceneObjectId> AddFreeScenePathSegment(
		RendererWindowState &windowState, const glm::vec3 &worldPosition, bool arrow);
	[[nodiscard]] Result<SceneObjectId> AddScenePathThroughSelectedAtoms(
		RendererWindowState &windowState, bool arrow);
	void SelectAddedScenePaths(RendererWindowState &windowState, std::vector<SceneObjectId> ids);

	// Clipboard shared across windows, same lifetime and shape as the orbital/plane ones.
	[[nodiscard]] std::vector<ScenePath> &GetScenePathClipboard();

	void CopyScenePathsToClipboard(const RendererWindowState &windowState);
	void DuplicateSelectedScenePaths(RendererWindowState &windowState);
	void PasteScenePathsFromClipboard(RendererWindowState &windowState);
	void EraseScenePaths(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);

	// Edit Mode topology actions. These resolve the active element from PathEditSession, route the
	// mutation through PathCommands and repair the element selection after topology changes.
	[[nodiscard]] Result<PathElementId> ExtendSelectedScenePathEnd(RendererWindowState &windowState);
	[[nodiscard]] Result<PathElementId> InsertSelectedScenePathSegment(RendererWindowState &windowState);
	[[nodiscard]] Result<void> DeleteSelectedScenePathNodes(RendererWindowState &windowState);
	[[nodiscard]] Result<void> SetSelectedScenePathHandleType(
		RendererWindowState &windowState, BezierHandleType type);
	[[nodiscard]] Result<void> ReverseEditedScenePath(RendererWindowState &windowState);
	[[nodiscard]] Result<PathArcParameters> ResolveSelectedScenePathArc(
		const RendererWindowState &windowState);
	[[nodiscard]] Result<void> ApplySelectedScenePathArc(
		RendererWindowState &windowState, const PathArcParameters &parameters);

	// Offsets every node and every cubic handle, drops the bindings and clears the persist key -
	// what a copy of a path has to have before it is inserted next to the original. A duplicate
	// that kept its bindings would be re-resolved straight back on top of the original on the next
	// frame, which looks like the duplicate silently failing.
	void OffsetAndDetachScenePath(ScenePath &path, const glm::vec3 &offset);
} // namespace DefectStudio
