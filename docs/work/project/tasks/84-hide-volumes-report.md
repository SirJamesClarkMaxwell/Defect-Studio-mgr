# Task 84 report - hide volumes

Branch `task/84-hide-volumes`, 2026-10-08. Contract by Claude, implementation by Codex (rounds
1-3 in one pass, `84b-hide-volumes-ui.md`), build and review by Claude. Task 83 (CHGCAR
densities) is merged into this branch (`850f62e`).

## How to try it

1. Open a structure, select the atom(s) of the defect.
2. Viewport Add > Bryła ukrywająca > Kula. Everything farther than 4 Å from the selection
   disappears (a new volume hides what is OUTSIDE - "keep the cluster, drop the bulk").
3. The volume is in the Scene Outliner under "Bryły ukrywające"; the N panel / Object Properties
   edits shape, frame (Å / fractional), inside/outside, anchor, centre, orientation, dimensions,
   colour, opacity and shows how many atoms it hides. Atoms update live while dragging.
4. The shape is drawn in the viewport only while the volume is selected.
5. Ctrl+C on the volume, switch to another structure window (e.g. the other charge state),
   select its defect atom, Ctrl+V: the volume is re-anchored on that selection. Without a
   selection it keeps the anchor indices that exist in the target (same supercell = same atom).
6. Ctrl+Z undoes every step; save + reopen keeps the volumes and the cut.

## Precedence rule as implemented

Effective atom visibility = `manual AND NOT covered`. `PushSelectionAndVisibilityToWindowState`
rebuilds the atom/bond flags from the ECS (the manual half: H, outliner eye, `HiddenSceneState`)
and then `ApplyHideVolumeMaskToWindowState` only clears flags. The mask is refreshed by every
editor mutator, by the outliner columns, by `RestoreSceneObjectsSnapshot` (undo/redo) and at the
end of `ApplyPersistedSceneObjects` (project load) - never per frame.

In practice: an atom hidden with H stays hidden whatever a volume does; deleting or switching off
a volume brings back exactly the atoms it was hiding; Alt+H shows the manually hidden atoms but
not the ones a volume covers - switch the volume's eye off for that. The camera column of a volume
cuts only from exports, so the bulk can stay on screen while it is dropped from a render.

## Files

- Geometry/mask/persistence (round 0, earlier): `Renderer/Scene/SceneHideVolume.{hpp,cpp}`,
  `IO/SceneObjectsHideVolumeYaml.cpp`, `IO/SceneObjectsIO.*`, `Renderer/Scene/SceneObjectPersistence*.cpp`,
  `Renderer/Scene/SceneSystem.cpp`.
- Mesh for the overlay: `BuildHideVolumeTriangles`, `HideVolumeWorldCenter` in `SceneHideVolume.cpp`.
- Mask refresh on undo/load: `Renderer/Commands/SceneObjectsSnapshotCommand.cpp`,
  `Renderer/Scene/SceneObjectPersistence.cpp`; selection clearing: `Renderer/Scene/SceneSelection.cpp`.
- UI: `Presentation/Panels/SceneHideVolumeEditor.{hpp,cpp}` (N panel on `PropertyGrid`, clipboard,
  add/erase/duplicate/re-anchor), `SceneOutlinerPanel.{hpp,cpp}`, `SceneOutlinerRender.cpp`,
  `SceneOutlinerRows.cpp` (two helpers moved to `SceneOutlinerPanel.cpp` for file size),
  `SceneObjectEditActions.{hpp,cpp}`, `ViewportLabelInteraction.cpp`, `ViewportAddMenu.cpp`,
  `ObjectPropertiesSelection.{hpp,cpp}`, `ObjectPropertiesPanel.cpp`.
- Viewport: `Renderer/OpenGl/OpenGlHideVolumeRenderer.cpp`, `OpenGlRendererBackend.{hpp,cpp}`
  (also frees the scene-plane mesh on shutdown, which leaked before), `RendererLayer.cpp`.
- Tests: `SceneHideVolumeTests` (14), `SceneHideVolumeMaskTests` (7), `SceneObjectsHideVolumeIOTests`
  (4), `SceneHideVolumeMeshTests` (5), `SceneHideVolumeEditorTests` (8),
  `GlTest.AHideVolumeIsDrawnOnlyWhileSelected`.

## Cut short / not done

- No viewport gizmo or picking for volumes: select in the outliner, move by anchoring on atoms or
  typing the centre. Add a gizmo when typing proves too slow.
- The overlay is a translucent fill, not a wireframe.
- Clipboard is in-process only (no OS clipboard / YAML export).
- Frame switch converts dimensions with the mean lattice-vector length - exact only for cubic cells.
- Euler orientation is re-derived from the matrix every frame; near gimbal lock the three numbers
  can jump while the orientation itself stays correct.
- Release only so far; Debug build + `full-build-verify` before merging to main.
