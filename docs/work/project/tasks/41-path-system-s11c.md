# Task 41 S11c: paths get an Object Mode UI

## Goal

S11a made a path clickable in the viewport; S11b gave paths the shared clipboard, delete and
visibility actions. S11c is the surface: a Paths group in the Scene Outliner with the same rows,
eye/camera columns and context menu every other kind has, a stroke editor in the Properties
panel, box/circle region select over paths, G/R/S moving a selected path's points, and the
creation presets behind the dev switch.

After this, a path behaves like an arrow or a plane everywhere a user can reach it, and the
path half of task/41's Object Mode is done.

## Files that are already written and ARE the contract

Do not change a signature, a struct field or a comment in any of these:

- `src/Presentation/Panels/ScenePathEditorWidget.hpp` (new, unimplemented)
- `src/Presentation/Panels/ViewportSelection.hpp` - the two `...ScenePaths` hit-tests and the new
  `pathHits` parameter on `ApplyLabelRegionSelection`
- `src/Renderer/Scene/SceneTransform.hpp` - `PathTransformStart` and
  `SceneTransformSelectionSnapshot::paths`
- `src/Presentation/Panels/ScenePathDevMenu.hpp` - the `profile` parameter on `MakeDevScenePath`
- `src/Presentation/Panels/ScenePathOperations.hpp` (S11b, already implemented)
- `src/Renderer/Path/ScenePathPicking.hpp` and `PathPicking.hpp` (S10/S11a, done and verified)

If you believe one of them is wrong, stop and say so instead of editing it.

## Files to create or change

1. `src/Presentation/Panels/ScenePathEditorWidget.cpp` (new) - all five functions from the header.
   `ResolveScenePathStyleEdit`, `ApplyScenePathStyleEdit`, `RenameScenePath` and
   `ScenePathDisplayName` are pure logic and are what the tests cover; `DrawScenePathEditor` is
   the ImGui half. Model the ImGui half on `SceneArrowEditorWidget`'s style section - same control
   vocabulary, same ordering - so the two panels do not look like they came from different apps.
2. `src/Presentation/Panels/ObjectPropertiesPanelSections.cpp` - a Paths section, drawn where the
   Planes section is drawn, gated on `ObjectPropertiesSections::paths` (S11b already sets it).
   It calls `DrawScenePathEditor`, plus the same Delete / Duplicate / Copy / Paste buttons the
   plane section has, through `ExecuteSceneObjectEditAction(..., SceneObjectEditKind::Path, ...)`
   (S11b already wired that kind).
3. `src/Presentation/Panels/SceneOutlinerPanel.{hpp,cpp}` and `SceneOutlinerRows.cpp` -
   a `drawPathsGroup`, modelled on `drawPlanesGroup`, plus `SelectionRowKind::Path` and its cases
   in `applyAnnotationRowSelection` and `drawSceneObjectContextMenu`. The group header's
   eye/camera columns need a path-store equivalent of `SceneVisibilityStateFor` /
   `ApplySceneVisibilityColumnEdit`: those are templates over a vector of objects with `.visible`
   and `.renderable`, and a PathStore is not a vector. Add overloads next to them rather than
   making the templates handle a store - the store's only mutable access is `MutateStyle`, and a
   template that hid that would be worse than two honest overloads.
   Row labels come from `ScenePathDisplayName`.
4. `src/Presentation/Panels/ViewportRegionSelect.cpp` - `HitTestRectScenePaths`,
   `HitTestCircleScenePaths`, and `ApplyLabelRegionSelection` honouring `pathHits`. Wire both into
   `HandleBoxSelectDrag` and `HandleCircleSelectDrag` alongside the arrow hits already there.
   Replace mode must clear `selectedScenePaths` with the other three.
5. `src/Renderer/Scene/SceneTransform.cpp` + a new
   `src/Renderer/Scene/SceneTransformPaths.cpp` - capture, apply and restore for
   `PathTransformStart`. SceneTransform.cpp is 463 lines and the repo limit is ~500, so the path
   bodies go in the new file and SceneTransform.cpp gets three one-line calls plus the
   `HasSceneObjectTransformTargets` / `SceneTransformPivotPositions` cases.
   Writing back goes through `PathDragTransaction` or `MoveScenePathNode`/`MoveScenePathHandle`
   from `Renderer/Path/PathCommands.hpp` - never by mutating the store directly, or the edit
   records no undo and no revision bump.
6. `src/Presentation/Panels/ScenePathDevMenu.cpp` - honour the new `profile` parameter and grow
   the submenu to Tube 3D / Flat ribbon / Camera-facing ribbon, each with Line / Cubic / Arc.
7. Tests (new files):
   - `tests/Presentation/Panels/ScenePathOperationsTests.cpp` - S11b shipped untested; cover it
     here.
   - `tests/Presentation/Panels/ScenePathEditorWidgetTests.cpp`
   - `tests/Renderer/Scene/SceneTransformPathsTests.cpp`
   Follow `tests/Presentation/Panels/SceneArrowOperationsTests.cpp` for how this repo builds a
   `RendererWindowState` in a test without a GL or ImGui context.

## Files that must NOT be touched

- `src/Renderer/Path/PathPicking.{hpp,cpp}`, `PathHandleGeometry.{hpp,cpp}`,
  `ScenePathPicking.{hpp,cpp}`, `PathCommands.{hpp,cpp}`, `PathStore.{hpp,cpp}`,
  `PathCaches.{hpp,cpp}` - S6 to S11a, all verified and committed.
- `src/Presentation/Panels/ViewportScenePathInteraction.cpp` - S11a.
- `src/Presentation/Panels/ScenePathOperations.cpp` - S11b. Test it, do not rewrite it. If a test
  you write fails against it, say so and stop; that is a real bug and I want to see it.
- `src/Domain/`, `src/IO/`, `src/App/`, `src/ScientificRuntime/`.
- Any existing test file.
- The OpenGL backend and the shaders - S11c adds no new drawing.

## Out of scope

- Edit mode: node dragging, per-node handles on screen, inserting or deleting a node from the
  viewport. That is S12 and the picker already has `editMode` for it.
- The numeric arc editor and per-node binding UI. S13 and S14.
- Deleting `ScenePathDevMenu.cpp`. Its own comment says whoever writes S11 should delete it
  rather than grow it, but the real creation UI is S15's job; growing it by one axis is the
  smaller change, and the "(dev)" label stays.
- Gradient and dash editing in the Properties panel. `ScenePathStyleEdit` deliberately has no
  field for either: they need their own multi-stop control, and that is not what this stage is.

## Acceptance criteria

`ScenePathEditorWidget` (in `ScenePathEditorWidgetTests.cpp`):

1. `ResolveScenePathStyleEdit` on an empty selection reports `resolved == 0` and leaves the
   defaults in place.
2. On one selected path it reports `resolved == 1`, that path's values, and no `mixed*` flag set.
3. On two paths differing only in `width` it sets `mixedWidth` and no other `mixed*` flag.
4. An id in the selection that resolves to no path does not count towards `resolved` and does not
   make a field look mixed.
5. `ApplyScenePathStyleEdit` writes every field to every selected path and returns the count.
6. It pushes exactly one undo entry for a multi-path edit, and undoing once restores every path.
7. `RenameScenePath` changes the name and returns false for an unknown id.
   *Amended after the dispatch: this criterion originally also demanded that renaming not bump
   the style revision. Codex correctly reported that as impossible - `MutateStyle` is PathStore's
   only mutable access. The bump is now documented as a deliberate ceiling on `RenameScenePath`
   in the header, with `MutateMetadata` as the upgrade path. The criterion was wrong, not the
   implementation.*
8. `ScenePathDisplayName` returns the name when set and `Path #<index>` when not.

`ScenePathOperations` (in `ScenePathOperationsTests.cpp`, covering the already-written S11b):

9. Copy then paste inserts new paths with new ids, leaving the originals in place.
10. A pasted path is offset, has no bindings and an empty `persistKey`.
11. Duplicating three selected paths pushes exactly one undo entry, and one undo removes all
    three.
12. Duplicate and paste leave `selectedScenePaths` holding the new ids, not the originals.
13. `EraseScenePaths` removes the paths, drops them from `selectedScenePaths`, and leaves ids it
    was not given alone.
14. Erasing is undoable in one step.

Region select (fold into `ScenePathOperationsTests.cpp` or its own file, your call):

15. `HitTestRectScenePaths` returns a path whose cached polyline crosses the rect, and nothing for
    one outside it.
16. Neither hit-test returns a hidden or non-renderable path.
17. Neither returns a path with no cached geometry.
18. `ApplyLabelRegionSelection` in Replace mode clears `selectedScenePaths`; in Add mode it adds
    without duplicating an id already selected; in Subtract mode it removes.

`SceneTransform` for paths (in `SceneTransformPathsTests.cpp`):

19. `CaptureSceneTransformSelection` fills `paths` with one entry per selected path, holding every
    node and every cubic handle, and nothing for an unselected path.
20. `HasSceneObjectTransformTargets` is true when only a path is selected.
21. A Translate delta moves every node and every handle by the same vector.
22. A Rotate delta about the selection pivot keeps the distance from each point to the pivot.
23. `RestoreSceneTransformSelection` puts every point back exactly, and leaves the store's
    geometry revision consistent (the restore is an edit, so it may bump - just do not leave a
    stale cache entry matching the new revision).
24. Every write went through PathCommands, and after a restore the paths hold their original
    coordinates.
    *Amended after the dispatch: this criterion originally also demanded that a transform leave
    undo history. It must not. `ViewportModalTransform` captures one `SceneObjectsSnapshot` when
    the modal begins and pushes it once on commit, and that snapshot already contains the whole
    PathStore - so `ApplySceneTransformPaths` runs on a context with no undo sink on purpose.
    Pushing there would create one undo entry per frame of a drag. The criterion named the wrong
    owner.*

Dev presets:

25. `MakeDevScenePath` with each `StrokeProfile` produces a path with that profile, and the
    default argument still produces the Round tube S7 produced.

Build and suite:

26. `scripts\Windows\GenerateProjects.bat` succeeds after the new files.
27. Release build of both `.vcxproj` targets is clean.
28. The full Release suite passes with exactly the two known `DS_PYTHON_CAPI_AVAILABLE=0` skips
    (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
    `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`). Baseline before S11c is
    826 tests / 165 suites.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `SceneTransformPaths` is Renderer and must not
  include anything from `Presentation`; the widgets are Presentation and must not reach into
  `Domain`.
- `Renderer` is the exception-free zone: no `throw` under `src/Renderer/`.
- No `std::thread` outside JobSystem.
- `.cpp` files stay under ~500 lines. `SceneOutlinerRows.cpp` is at 458 and
  `ObjectPropertiesPanelSections.cpp` at 482 - if a section does not fit, split it into a new
  `.cpp` beside it rather than going over.
- `near` and `far` are Windows macros. Never use them as identifiers.
- Do not tessellate in a hit-test. Read `PathCaches::FindLastBuilt`, same as S11a.
- Every store mutation goes through `PathCommands`. Reaching into `PathStore::MutateGeometry`
  directly from Presentation skips validation, undo and the revision bump.

## Manual round

With a structure loaded: add paths of each profile and segment kind; select one in the viewport
and confirm the Outliner row highlights, and the reverse; rename one; change width, colour and
end decoration on a two-path selection; box-select over two paths; G/R/S a selected path and
Ctrl+Z it; hide one with the eye, drop another from the camera column, export and confirm which
came out; delete, copy, paste, duplicate. Then save, reopen, and confirm every path came back.
