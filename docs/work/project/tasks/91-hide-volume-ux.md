# Task 91: hide volumes after the first real use

Branch `task/91-hide-volume-ux` off `task/90-hide-volume-gizmo`. User feedback after trying task 84/90
in the app (2026-10-08), item by item:

1. A volume can only be selected in the Scene Outliner. Clicking it in the viewport must select it.
2. The volume should be a wireframe, not a translucent solid.
3. After resizing a volume the uncovered atoms did not come back until the outliner was clicked.
4. A strange long "bond" appeared (under investigation separately).
5. Vacancies, the wavefunction and the charge density, and labels must be hidden by a volume too.
6. A popup like the one after adding a C_n arrow (Operator Redo panel) would help.
7. Hiding the volume (its eye) must not switch its effect off.
8. Adding a volume on a given atom must be easy.

## Contract (written by Claude, do not change)

`src/Renderer/Scene/SceneHideVolume.hpp`:

- `SceneHideVolume::active` - the cut on/off. `visible` / `renderable` (the outliner's eye / camera)
  now only draw the outline in the viewport / in an exported image. `renderable` defaults to false.
- `BuildHideVolumeEdges(volume, structure)` - world-space line list (pairs) of the outline.
- `HideVolumeWorldToUnit(volume, structure)` - world -> unit shape affine map, for a shader clip.
  Sphere |p| <= 1, box max|p_i| <= 1, cylinder |p.xy| <= 1 and |p.z| <= 1; `invert` not folded in.
- `PointCoveredByHideVolumes(volumes, point, structure)` - any active volume covers the point.
- `ApplyHideVolumeMaskToWindowState` - an active volume cuts atoms and bonds, `visible` and
  `renderable` alike.

Item 3 root cause (fixed by Claude): undo/view snapshots read hidden atoms from the structure mirror,
which also carries the volume's cut, and wrote them back to the ECS as a manual H. They now read
`CaptureHiddenSceneState(const RendererWindowState &)`, which reads VisibilityComponent.

Persistence: `active` in `scene_objects.yaml`; a file without it takes `active` from `visible`.

## Split

- Renderer (Codex A): wireframe overlay, vacancy markers and labels hidden when covered, isosurface
  clip for densities and orbitals.
- Presentation (Codex B): viewport click selection, the operator popup, add-at-atom, `active` in
  the UI, vacancy/label picking skips covered ones.

## Done (commit db71480) and how to try it

- Add > Bryła ukrywająca (or right-click an atom > Ukryj wokół atomu): the Operator Redo popup opens
  with kind, frame, size and "Ukryj na zewnątrz"; changing a value replaces the volume just added.
- The outline is a wireframe, drawn while the eye is on, brighter when selected. Clicking a line of
  it selects the volume; an atom or bond under the cursor still wins.
- Eye off: the cage disappears, the atoms stay hidden. "Aktywna (ukrywa)" in Object Properties, or
  the outliner row's context menu, switches the cut. An inactive cage is drawn at half opacity.
- Inside an active volume: vacancy markers and their labels, free labels, pinned measurements, paths
  bound to a hidden atom (vacancy bonds) and the density / orbital isosurfaces disappear, and none of
  them can be clicked or region-selected.
- Resizing a volume after aligning the view to an axis, adding a vacancy or H / Alt+H brings the
  uncovered atoms back (item 3).
- "Wiązania do sąsiadów" picks the physical neighbour shell even when a volume covers it (item 4,
  most likely cause - a vacancy bond to the nearest *visible* atom).

Not done: the camera eye defaults to off for new volumes (no cage in exports); the isosurface clip
takes the first eight active volumes.
