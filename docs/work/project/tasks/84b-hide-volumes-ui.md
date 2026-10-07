# Task 84b: hide volumes - UI, viewport overlay, copy/paste

Branch `task/84-hide-volumes` (task 83 densities already merged into it). Rounds 1-3 of
`84-hide-volumes-handoff.md`. Geometry, the mask and persistence are done and green
(`SceneHideVolumeTests`, `SceneHideVolumeMaskTests`, `SceneObjectsHideVolumeIOTests`).

## Goal

A user can add a sphere / box / cylinder hide volume from the viewport Add menu, see it in the Scene
Outliner with its two columns (eye, camera), edit it in the N panel / Object Properties, see its
shape in the viewport while it is selected, delete / duplicate / copy / paste it (Ctrl+C in one
structure window, Ctrl+V in another), and undo all of it. Atoms hidden by a volume disappear the
moment the volume changes, and come back on undo.

## The precedent to copy: task 83 densities

Commit `0cacf82` added the `SceneDensity` scene object end to end. Hide volumes need the SAME
touch points, so read it first: `git show 0cacf82 -- src/Presentation src/Renderer`. In particular:
`SceneOutlinerRows.cpp` (`drawDensitiesGroup`), `SceneOutlinerRender.cpp`, `SceneOutlinerPanel.{hpp,cpp}`
(`SelectionRowKind::Density`), `SceneObjectEditActions.{hpp,cpp}`, `ViewportLabelInteraction.cpp`,
`ObjectPropertiesSelection.{hpp,cpp}`, `ObjectPropertiesPanel.cpp`, `ViewportAddMenu.cpp`,
`SceneSelection.cpp` (`ClearAllSceneSelection`), `RendererLayer.cpp` (selected-index plumbing into
`RenderWindow`), `OpenGlRendererBackend.{hpp,cpp}` + `OpenGlDensityRenderer.cpp`, and
`SceneDensityEditor.cpp` for the N panel built on `PropertyGrid`.

The viewport overlay copies `OpenGlScenePlaneRenderer.cpp` instead: a CPU triangle soup of
`IsosurfaceVertex` uploaded to a VBO and drawn with `renderIsosurfaceGpuOverlay`. No new shader,
no new pass.

## The contract (already written - do not change signatures or test expectations)

- `src/Renderer/Scene/SceneHideVolume.hpp`: new `BuildHideVolumeTriangles`, `HideVolumeWorldCenter`.
- `src/Presentation/Panels/SceneHideVolumeEditor.hpp`: the whole UI API, documented per function.
- `src/Renderer/RendererWindowState.hpp`: `selectedSceneHideVolumes`.
- Tests: `tests/Renderer/Scene/SceneHideVolumeMeshTests.cpp`, `tests/Renderer/SceneHideVolumeEditorTests.cpp`.

If a signature looks wrong, stop and say so in the report instead of changing it.

## Do

1. **Geometry** (`src/Renderer/Scene/SceneHideVolume.cpp`, append; stays under ~500 lines):
   - `HideVolumeWorldCenter`: anchor centroid if it resolves, else `center`; `Fractional` maps the
     centre through `structure.lattice` (columns are the lattice vectors, `PrepareHideVolume`
     already uses `inverse(lattice)` for world->frame).
   - `BuildHideVolumeTriangles`: build the unit shape in the volume's local space, then map each
     vertex local -> world with exactly the inverse of `PointInPreparedHideVolume`'s transform:
     `world = M * (frameCentre + orientation * (halfExtents-scaled local))`, `M = lattice` for
     `Fractional`, identity for `Anchored`. Sphere: UV sphere, 24 slices x 12 stacks. Box: 12
     triangles. Cylinder: 32-segment side + two fan caps, axis = local z. Outward winding. Empty for
     a singular lattice (reuse `kSingularDeterminantEpsilon`) or any non-positive used dimension
     (sphere: x; box: x,y,z; cylinder: x,z). Ignore `invert`.
2. **Mask refresh on restore and load** (root cause, not per caller):
   - `RestoreSceneObjectsSnapshot` (`src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp`):
     `KeepExistingSelection(window.selectedSceneHideVolumes, window.sceneHideVolumes)` and, at the
     end, `SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window)`.
   - `ApplyPersistedSceneObjects` (`src/Renderer/Scene/SceneObjectPersistence.cpp`): clear
     `selectedSceneHideVolumes` with the other selections, and push at the end the same way.
   - `ClearAllSceneSelection` (`src/Renderer/Scene/SceneSelection.cpp`): clear `selectedSceneHideVolumes`.
3. **Editor** - new `src/Presentation/Panels/SceneHideVolumeEditor.cpp` implementing the header.
   Every mutator: `PushPinnedMeasurementUndoSnapshot(windowState)` first (one undo step), then the
   change, then `SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry,
   windowState)`. New ids from `windowState.sceneRegistry.AllocateObjectId()`. Clipboard is a
   function-local static `std::vector<SceneHideVolume>` like `GetSceneFreeLabelClipboard`.
   Names in Polish with diacritics: "Kula N", "Prostopadłościan N", "Walec N" (UTF-8 source).
4. **N panel section** `DrawSelectedSceneHideVolumeSection`, built on `PropertyGrid` exactly like
   `DrawSelectedSceneDensitySection` (fixed left label column, `PropertyGridLabelWidth` over all
   labels, `CollapsingHeader` per group, open by default). Edits of a slider/drag push ONE undo
   snapshot when the item is activated (`ImGui::IsItemActivated()`), not per frame, then refresh
   the mask on every change so atoms update live while dragging. Groups and rows:
   - **Bryła**: Nazwa (InputText); Kształt (combo Kula / Prostopadłościan / Walec); Układ (combo
     "Zakotwiczony (Å)" / "Ułamkowy (sieć)"); Ukrywa (combo "Wewnątrz" / "Na zewnątrz" = invert).
   - **Położenie**: Kotwica - text "N atomów" or "brak", buttons "Z zaznaczenia"
     (`AnchorSelectedSceneHideVolumesToSelectedAtoms`) and "Odkotwicz"; Środek (DragFloat3, unit
     suffix " Å" or none; read-only showing the resolved centroid while anchors resolve);
     Orientacja (Box and Cylinder only): DragFloat3 Euler degrees converted the way persistence
     does (`glm::eulerAngles(quat_cast(orientation))` / `mat3_cast(quat(radians(euler)))`), plus
     for Cylinder axis buttons "X" "Y" "Z" and "Z 2 atomów" (axis along the first two selected
     atoms; disabled unless exactly 2 are selected).
   - **Wymiary**: only the active kind's fields. Sphere: Promień. Box: Wymiary (full side lengths =
     2 * halfExtents, DragFloat3). Cylinder: Promień, Wysokość (full = 2 * halfExtents.z). Minimum
     0.1 Å (0.01 in Fractional). Units " Å" or none.
   - **Wygląd**: Kolor (ColorEdit3), Krycie (SliderFloat 0..1).
   - **Wynik**: Ukryte atomy - `CountAtomsHiddenBySceneHideVolume`, read-only `PropertyGrid::Value`.
   Switching Układ converts in place so the volume does not jump: centre through `inverse(lattice)`
   / `lattice`, dimensions divided / multiplied by the mean lattice-vector length; going to
   Fractional clears `anchorAtoms` (anchors are Anchored-only). `ponytail:` mean length is exact
   only for cubic cells - fine for a starting point the user then edits.
5. **Scene Outliner**: group "Bryły ukrywające (n)", shown only when non-empty, one row per volume
   with the eye (`visible`) and camera (`renderable`) columns, click-to-select (clears other
   selection kinds, like densities), row context menu Delete / Duplicate / Copy / Paste. Toggling
   either column: undo snapshot, flip, refresh mask. New `SelectionRowKind::HideVolume`.
6. **Edit actions**: `SceneObjectEditKind::HideVolume`, all four actions, wired to the editor
   functions; `ClipboardHasObjects` uses `GetSceneHideVolumeClipboard()`. Viewport Delete /
   Ctrl+D / Ctrl+C / Ctrl+V reach it through `ViewportLabelInteraction.cpp` /
   `ResolveSelectedDrawingKind`, as densities do.
7. **Add menu** (`ViewportAddMenu.cpp`): submenu "Bryła ukrywająca" with "Kula", "Prostopadłościan",
   "Walec" -> `AddSceneHideVolume`.
8. **Object Properties**: `sections.hideVolumes` in `ObjectPropertiesSelection`, drawn via
   `DrawSelectedSceneHideVolumeSection` in `ObjectPropertiesPanel.cpp`.
9. **Viewport overlay**: new `src/Renderer/OpenGl/OpenGlHideVolumeRenderer.cpp` with
   `OpenGlRendererBackend::renderSceneHideVolumes(...)`, called next to `renderScenePlanes`. Draw
   ONLY the selected volumes (a volume drawn while merely visible would sit on top of the defect
   it is framing). Fill = `BuildHideVolumeTriangles` -> `IsosurfaceVertex` with the flat triangle
   normal and sign 1, drawn with `renderIsosurfaceGpuOverlay(..., volume.color, volume.color * 0.45f,
   volume.alpha, sceneOffset, false, 0.0f, false, true)` like a plane; own `OpenGlMeshHandles` in
   `OpenGlViewportResources`, freed in shutdown like `scenePlaneMesh`. Pass
   `windowState.sceneHideVolumes` + selected indices through `RenderWindow` from `RendererLayer.cpp`
   the way densities are passed (append trailing parameters with defaults so other callers, e.g.
   `tests/Renderer/Gl/*`, still compile). Exports never draw volumes - the mask already reaches
   them through the atoms' `renderable` flags.

## Files to create or change

Create: `src/Presentation/Panels/SceneHideVolumeEditor.cpp`, `src/Renderer/OpenGl/OpenGlHideVolumeRenderer.cpp`.

Change: `src/Renderer/Scene/SceneHideVolume.cpp`, `src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp`,
`src/Renderer/Scene/SceneObjectPersistence.cpp`, `src/Renderer/Scene/SceneSelection.cpp`,
`src/Renderer/OpenGl/OpenGlRendererBackend.{hpp,cpp}`, `src/Renderer/RendererLayer.cpp`,
`src/Presentation/Panels/SceneOutlinerPanel.{hpp,cpp}`, `SceneOutlinerRows.cpp`, `SceneOutlinerRender.cpp`,
`SceneObjectEditActions.{hpp,cpp}`, `ViewportLabelInteraction.cpp`, `ViewportAddMenu.cpp`,
`ObjectPropertiesSelection.{hpp,cpp}`, `ObjectPropertiesPanel.cpp`.

## Files that must NOT be touched

- `src/Renderer/Scene/SceneHideVolume.hpp`, `src/Presentation/Panels/SceneHideVolumeEditor.hpp`,
  every test file - they are the contract
- anything under `src/Domain/`
- `src/Renderer/Scene/ViewModifier.cpp`, `HiddenSceneState.*`, `SceneVisibility.*` - the manual H
  half stays exactly as it is. H / Alt+H do NOT act on volumes: a volume is switched off by its own
  eye (decided with the user).
- `src/Presentation/Operators/**`, `OperatorRedoPanel.*`, `CurvedArrowParameters.hpp`
- `IsosurfaceMesher.*`, `SceneOrbitalGeometry.*`, density/orbital IO, `SceneDensity*`
- `premake5.lua`, `build/`, `install/`

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=SceneHideVolume*:SceneObjectsHideVolume*` all green; no
   test expectation or contract signature changed.
2. Full Release suite: no new failures (expected skips: ConPty, `GlTest.RealChgcarSpinDensityRendersToArtifact`).
3. Every new `.cpp` under ~500 lines; `SceneHideVolume.cpp` still includes nothing from
   Presentation / App / Domain.
4. In the app: Add > Bryła ukrywająca > Kula with one atom selected hides everything farther than
   4 Å; the sphere is drawn while selected; radius drag updates atoms live; Ctrl+Z restores; the
   eye column toggles the cut; Ctrl+C / Ctrl+V into another structure window works; save + reopen
   keeps the cut.

## Constraints

- Renderer files: no ImGui, no Presentation/App includes. Presentation may include Renderer.
- No exceptions in render paths. No per-frame mask recomputation - only on change.
- `glm` only; no new dependency.
- Match surrounding code: tabs, `[[nodiscard]]`, comment density of the neighbouring files.
