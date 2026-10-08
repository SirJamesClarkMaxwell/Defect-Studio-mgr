#pragma once

#include <vector>
#include "Presentation/Operators/SceneOperator.hpp"

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneHideVolume.hpp"

namespace DefectStudio
{
	// task/84: everything the UI does with SceneHideVolume. Geometry and the mask itself live in
	// Renderer/Scene/SceneHideVolume.hpp; these only create, edit, copy and remove the objects.
	//
	// Every function here that changes `windowState.sceneHideVolumes` (or a volume in it) ends with
	// SceneSystem::PushSelectionAndVisibilityToWindowState, which rebuilds the atom/bond flags from
	// the ECS and re-applies the mask. That is the only way the mask is refreshed - nothing
	// recomputes it per frame - so a mutator that forgets it leaves stale hidden atoms on screen.

	// Default radius of a new volume around a single atom: first and second shells in diamond
	// (1.54 / 2.52 A) and a margin, so "select the vacancy neighbour, Add > Sphere" already shows
	// the defect cluster.
	inline constexpr float kDefaultHideVolumeRadius = 4.0f;

	// A new volume of `kind`, not yet in `windowState`:
	// - with atoms selected: Anchored on them (anchorAtoms = selectedAtomIndices), radius =
	//   max(kDefaultHideVolumeRadius, largest distance from their centroid + 1 A);
	// - without: anchor on targetAtom if valid, else use targetPosition (legacy calls: cell centre).
	// `invert` is ON - the common case is "keep the cluster, drop the bulk". Box gets that radius as
	// all three half-sides, cylinder as radius and half-height (axis z). Name "Kula N" /
	// "Prostopadloscian N" / "Walec N" (Polish, with diacritics), N = count of that kind + 1.
	[[nodiscard]] SceneHideVolume MakeSceneHideVolume(const RendererWindowState &windowState, HideVolumeKind kind,
		std::optional<std::size_t> targetAtom = std::nullopt, std::optional<glm::vec3> targetPosition = std::nullopt);

	// MakeSceneHideVolume + allocate id + push (one undo step), select it alone, refresh the mask.
	void AddSceneHideVolume(RendererWindowState &windowState, HideVolumeKind kind,
		std::optional<std::size_t> targetAtom = std::nullopt, std::optional<glm::vec3> targetPosition = std::nullopt);
	SceneObjectId AddSceneHideVolume(RendererWindowState &windowState, SceneHideVolume volume,
		SceneOperationUndo undo = SceneOperationUndo::Push);
	void SetSceneHideVolumeActive(RendererWindowState &windowState, SceneObjectId id, bool active);

	void EraseSceneHideVolumes(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);
	// Same anchors and dimensions, new id, empty persistKey; the copies become the selection.
	void DuplicateSelectedSceneHideVolumes(RendererWindowState &windowState);

	// In-process clipboard shared by every structure window - this is how a per-material preset
	// moves from GeV q0 to GeV q-1. `ponytail:` no OS clipboard; emit the YAML when someone needs it
	// across projects.
	[[nodiscard]] std::vector<SceneHideVolume> &GetSceneHideVolumeClipboard();
	// Copies the selected volumes with `center` replaced by the resolved anchor centroid, so a paste
	// whose anchors do not resolve still lands where the copy was.
	void CopySceneHideVolumesToClipboard(const RendererWindowState &windowState);
	// Pastes the clipboard as new selected volumes (one undo step). Anchors, for Anchored volumes:
	// - atoms selected in the target: re-anchor every pasted volume on that selection - "select the
	//   defect in the other cell, Ctrl+V";
	// - otherwise keep the anchor indices that are in range of the target structure and drop the
	//   rest (same supercell, other charge state: same indexing, so this is the right atom).
	void PasteSceneHideVolumesFromClipboard(RendererWindowState &windowState);

	// Re-anchors the selected Anchored volumes on the selected atoms (no atoms selected: clears the
	// anchor and keeps the volume where it is). One undo step, mask refreshed.
	void AnchorSelectedSceneHideVolumesToSelectedAtoms(RendererWindowState &windowState);

	// How many atoms this volume alone hides (eye ignored), for the N panel statistics row.
	[[nodiscard]] std::size_t CountAtomsHiddenBySceneHideVolume(
		const RendererWindowState &windowState, const SceneHideVolume &volume);

	// The N panel / Object Properties section for the first selected volume.
	void DrawSelectedSceneHideVolumeSection(RendererWindowState &windowState);
} // namespace DefectStudio
