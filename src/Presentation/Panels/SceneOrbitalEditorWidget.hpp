#pragma once

#include <cstddef>
#include <vector>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// The Object Properties panel's "Orbitals" section. Lives in its own file because
	// ObjectPropertiesPanel.cpp was already past the ~500-line limit in AGENTS.md before this
	// existed, and because the quick-edit popup will want the same editor later - the same reason
	// SceneArrowEditorWidget.hpp exists next to it.

	// Every control for one orbital: preset (grouped the same way the Add menu is), shell, lobe,
	// effective charge, iso fraction, sampling resolution, figure scale, rotation, both centres,
	// the two phase colours, alpha, and its anchoring. Pushes one undo snapshot per logical edit.
	//
	// Re-meshing happens in the renderer when MakeSceneOrbitalMeshKey changes, so nothing here has
	// to ask for it - but every field below is in that key, which is why dragging `resolution` is
	// the one control that can visibly hitch: meshing is on the main thread.
	void DrawSceneOrbitalEditor(RendererWindowState &windowState, std::size_t orbitalIndex);
	void DrawScenePlaneEditor(RendererWindowState &windowState, std::size_t planeIndex);

	// Selection-focused editors used by Object Properties' primary view. These never enumerate
	// unselected objects, so selecting one arrow cannot leave a wall of orbital/plane controls below
	// it. The full lists remain available through the panel's "Wszystkie obiekty" drawer.
	void DrawSelectedSceneOrbitalSection(RendererWindowState &windowState);
	void DrawSelectedScenePlaneSection(RendererWindowState &windowState);

	// Copies the first selected single-centre orbital's orientation to the other selected
	// single-centre orbitals. Two-centre presets derive their frame from their centres and are
	// deliberately left untouched.
	void AlignSelectedSceneOrbitalOrientations(
		std::vector<RendererWindowState::SceneOrbital> &orbitals,
		const std::vector<SceneObjectId> &selection);

	// Scene-wide select/remove rows used only inside "Wszystkie obiekty". Selecting a row makes its
	// editor appear in the primary selection-focused area on the next frame.
	void DrawAllSceneOrbitalRows(RendererWindowState &windowState);
	void DrawAllScenePlaneRows(RendererWindowState &windowState);

	// The "Plaszczyzny" section: one row per ScenePlane with select/remove chrome, and for the
	// selected one its centre, orientation, size, colour, alpha and border. Kept beside the orbital
	// section because both are scene objects the properties panel grew after it was already too
	// large to take another one inline.
	// Sorts descending, de-duplicates, ignores out-of-range ids, and clears the selection - same
	// contract as EraseSceneArrows, including that the caller pushes the undo snapshot first.
	void EraseSceneOrbitals(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);
	void EraseScenePlanes(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);

	// Process-wide whole-object clipboards, matching SceneArrow's copy/duplicate/paste contract.
	// Copies receive fresh ids, lose persistence keys, move by a small world-space offset and detach
	// from atoms so the anchor resolver cannot snap them back onto the source object.
	[[nodiscard]] std::vector<RendererWindowState::SceneOrbital> &GetSceneOrbitalClipboard();
	void CopySceneOrbitalsToClipboard(const RendererWindowState &windowState);
	void DuplicateSelectedSceneOrbitals(RendererWindowState &windowState);
	void PasteSceneOrbitalsFromClipboard(RendererWindowState &windowState);

	[[nodiscard]] std::vector<RendererWindowState::ScenePlane> &GetScenePlaneClipboard();
	void CopyScenePlanesToClipboard(const RendererWindowState &windowState);
	void DuplicateSelectedScenePlanes(RendererWindowState &windowState);
	void PasteScenePlanesFromClipboard(RendererWindowState &windowState);
} // namespace DefectStudio
