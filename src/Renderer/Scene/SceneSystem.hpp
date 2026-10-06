#pragma once

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/Scene/SceneRegistry.hpp"

namespace DefectStudio
{
	// The ECS<->flat-array boundary. RendererStructureData::atoms/bonds and
	// RendererWindowState::selectedAtomIndices stay the GPU-instanced-rendering hot path
	// (OpenGlRendererBackend reads them unchanged) - SceneSystem is the only place that syncs
	// ECS component state into them.
	namespace SceneSystem
	{
		// (Re)builds one entity per atom/bond from `structure`, destroying any entities from a
		// previous structure first. Call whenever a window's structure is (re)loaded.
		void SyncSceneWithStructure(SceneRegistry &scene, const RendererStructureData &structure);

		// Reads SelectionComponent/VisibilityComponent off every atom entity and writes the
		// result into windowState.selectedAtomIndices / windowState.structure.atoms[i].visible.
		// Every mutation path (click/box/circle-select, hide/show-all/view-modifiers) ends with
		// this call.
		void PushSelectionAndVisibilityToWindowState(const SceneRegistry &scene, RendererWindowState &windowState);

		// Deselects every atom and bond (entities + window mirrors). A plain click on an annotation
		// replaces the selection, so a stale atom selection can't drag the transform pivot away.
		void ClearStructureSelection(SceneRegistry &scene, RendererWindowState &windowState);

		// The reverse of PushSelectionAndVisibilityToWindowState - sets SelectionComponent/
		// VisibilityComponent from index lists (e.g. a restored RendererViewSnapshot). Caller is
		// responsible for following up with PushSelectionAndVisibilityToWindowState to sync the
		// flat arrays back, per the same contract as every other mutation path. Bond lists default
		// empty - existing callers (view snapshot restore) don't track bond selection/visibility yet.
		void ApplySelectionAndVisibilityToScene(
			SceneRegistry &scene,
			const std::vector<std::size_t> &selectedAtomIndices,
			const std::vector<std::size_t> &hiddenAtomIndices,
			const std::vector<std::size_t> &selectedBondIndices = {},
			const std::vector<std::size_t> &hiddenBondIndices = {});

		// Translates captured positions into atom indices on `targetStructure` by nearest cartesian
		// distance, for restoring a view snapshot onto a structure that may differ from the one it
		// was captured on (e.g. the session default view applied to a different window). A position
		// with no atom within `tolerance` is dropped rather than mapped to a wrong atom. Distances use
		// the minimum image of targetStructure.lattice (periodic and not singular), so a site on a cell
		// face matches whichever side the reloaded file wrapped it to.
		[[nodiscard]] std::vector<std::size_t> ResolveAtomIndicesByPosition(
			const RendererStructureData &targetStructure,
			const std::vector<glm::vec3> &positions,
			float tolerance = 0.35f);

		// Resolves a pinned label's live world position (measurement anchor + worldOffset). Shared by
		// ECS synchronization and the modal-transform adapter so both use identical anchor semantics.
		[[nodiscard]] bool ResolvePinnedMeasurementPosition(
			const RendererStructureData &structure,
			const RendererWindowState::PinnedMeasurement &pin,
			glm::vec3 &outPosition);

		// (Re)builds one entity per annotation - pinned measurement, free label, path and scene
		// orbital - from the vectors on windowState, destroying the previous set first; same "resync on
		// structural change" shape as SyncSceneWithStructure, not a per-frame rebuild. Call after any
		// add/remove on any annotation vector. Since task 20 it also assigns a SceneObjectId to any
		// annotation whose id is still unset, so an object created without one is addressable from the
		// next sync on.
		void SyncLabelEntities(SceneRegistry &scene, RendererWindowState &windowState);

		// task/41: the window's PathSystem, created on first use. Every consumer goes through this
		// rather than dereferencing windowState.paths, so a window that has never held a path costs
		// nothing and no call site has to repeat the null check.
		[[nodiscard]] PathSystem &EnsurePathSystem(RendererWindowState &windowState);
		[[nodiscard]] BindingContext MakePathBindingContext(const RendererWindowState &windowState);

		// The id on `path` is discarded and a fresh one
		// allocated, because a duplicated path arrives still carrying its source's id and one id maps to
		// one entity. Does not call SyncLabelEntities and does not touch selection. Returns the allocated
		// id on success, or an unset id when PathStore rejects the insert (for example, an id collision).
		// Undo restoration deliberately keeps original ids and so goes through PathSystem::ReplaceStore
		// instead.
		SceneObjectId AppendScenePath(RendererWindowState &windowState, ScenePath path);

		// Ids -> positions in the matching flat array, for the code that must still speak indices:
		// OpenGlRendererBackend's signatures are deliberately untouched by task 20, so the render call
		// sites convert right before the draw. Ids that no longer resolve are dropped rather than
		// mapped to a wrong object - that is the whole point of the id.
		[[nodiscard]] std::vector<std::size_t> ResolveSourceIndices(
			const SceneRegistry &scene, const std::vector<SceneObjectId> &ids);

		// Refreshes every label entity's TransformComponent.position from its pin's CURRENT anchor
		// (bond midpoint / angle vertex) + worldOffset. Cheap enough to call every frame - pinned
		// measurements are few, and the anchor moves with the atoms it measures (gizmo drag, nudge,
		// relaxation playback), so a stale transform would visibly lag behind the label's own draw.
		void UpdateLabelTransforms(SceneRegistry &scene, const RendererWindowState &windowState);

		// Mirrors windowState.selectedPinnedMeasurements into the matching label entities'
		// SelectionComponent (all others cleared). Call after any change to selectedPinnedMeasurements.
		void SyncLabelSelection(SceneRegistry &scene, const RendererWindowState &windowState);
	} // namespace SceneSystem
} // namespace DefectStudio
