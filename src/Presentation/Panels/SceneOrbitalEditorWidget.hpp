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

	// The whole section: one collapsible row per orbital with select/remove chrome, plus the
	// "N orbitals selected" bulk note. Draws nothing when there are no orbitals.
	void DrawSceneOrbitalSection(RendererWindowState &windowState);

	// The "Plaszczyzny" section: one row per ScenePlane with select/remove chrome, and for the
	// selected one its centre, orientation, size, colour, alpha and border. Kept beside the orbital
	// section because both are scene objects the properties panel grew after it was already too
	// large to take another one inline.
	void DrawScenePlaneSection(RendererWindowState &windowState);

	// Sorts descending, de-duplicates, ignores out-of-range ids, and clears the selection - same
	// contract as EraseSceneArrows, including that the caller pushes the undo snapshot first.
	void EraseSceneOrbitals(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);
} // namespace DefectStudio
