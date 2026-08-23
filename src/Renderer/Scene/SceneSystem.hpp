#pragma once

#include "Renderer/RendererTypes.hpp"
#include "Renderer/Scene/SceneRegistry.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

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
		// with no atom within `tolerance` is dropped rather than mapped to a wrong atom.
		[[nodiscard]] std::vector<std::size_t> ResolveAtomIndicesByPosition(
			const RendererStructureData &targetStructure,
			const std::vector<glm::vec3> &positions,
			float tolerance = 0.35f);

		// (Re)builds one entity per pinned measurement from windowState.pinnedMeasurements, destroying
		// any label entities from a previous sync first - same "resync on structural change" shape as
		// SyncSceneWithStructure, not a per-frame rebuild. Call after any pin add/remove.
		void SyncLabelEntities(SceneRegistry &scene, RendererWindowState &windowState);

		// Refreshes every label entity's TransformComponent.position from its pin's CURRENT anchor
		// (bond midpoint / angle vertex) + worldOffset. Cheap enough to call every frame - pinned
		// measurements are few, and the anchor moves with the atoms it measures (gizmo drag, nudge,
		// relaxation playback), so a stale transform would visibly lag behind the label's own draw.
		void UpdateLabelTransforms(SceneRegistry &scene, const RendererWindowState &windowState);

		// Mirrors windowState.selectedPinnedMeasurement into the matching label entity's
		// SelectionComponent (all others cleared). Call after any change to selectedPinnedMeasurement.
		void SyncLabelSelection(SceneRegistry &scene, const RendererWindowState &windowState);
	} // namespace SceneSystem
} // namespace DefectStudio
