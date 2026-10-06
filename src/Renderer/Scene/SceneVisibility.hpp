#pragma once

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// The Scene Outliner's two columns, Blender's split: `visible` is the eye (drawn in the
	// viewport, what H toggles), `renderable` is the camera (drawn in an exported render). They are
	// independent - clearing the camera leaves an object on screen and omits it from the export,
	// and hiding with H does not change what a render contains.
	//
	// These functions cover the scene objects that live as plain vectors on RendererWindowState:
	// pinned measurements, free labels, paths, orbitals and planes, plus the defect axes' show flag. Atoms and bonds carry the same
	// two flags but are owned by the ECS mirror (VisibilityComponent ->
	// SceneSystem::PushSelectionAndVisibilityToWindowState), so writing their flags here would be
	// overwritten on the next push - the atom/bond half of H stays in HideSelectionModifier /
	// ShowAllModifier, and the H handler calls both.
	//
	// Renderer layer: no ImGui, no Presentation includes.

	// True when anything the Hide/Show-all actions can act on is selected - atoms included, so a
	// menu item can use this for its enabled state without asking twice.
	[[nodiscard]] bool AnySceneObjectSelected(const RendererWindowState &windowState);

	// Sets the eye column on every selected scene object. The camera column is left alone.
	void SetSelectedSceneObjectsVisible(RendererWindowState &windowState, bool visible);

	// Clears the eye column on every scene object, selected or not. The camera column is left alone.
	void ShowAllSceneObjects(RendererWindowState &windowState);

	// Collapses the two columns onto the one flag the renderer reads, for an export snapshot: what a
	// render contains is the camera column alone. Call this on the copy filled for the export pass,
	// never on the live window state - it destroys the eye column by overwriting it.
	void ApplyRenderPassVisibility(RendererWindowState &windowState);
} // namespace DefectStudio
