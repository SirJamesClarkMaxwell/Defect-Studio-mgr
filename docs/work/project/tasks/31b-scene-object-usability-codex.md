# Task 31b: scene-object usability - gizmo, outliner icons, multi-selection editing

Split out of `31-scene-object-usability.md` (the triage of 14 items reported from hands-on
testing). Items #1, #3, #4, #5, #6, #8, #9, #10, #11 are already done on this branch. This task
covers the four that are left.

## Goal

After this task:

1. The transform gizmo (G/R/S and the toolbar's Move/Rotate/Scale) moves, rotates and scales
   selected **orbitals and planes**, exactly as it already does for atoms, labels and arrows.
2. The Scene Outliner's two visibility columns are **icons on the right-hand side of each row**
   (Blender's eye and camera columns), not checkboxes on the left.
3. Selecting **several objects of the same kind** in the properties panel lets the user edit the
   properties they share, instead of showing only the first one or nothing at all.
4. The `Add > Orbital` menu is readable: it currently drops a long list of presets on the user.

## Files to create or change

- `src/Renderer/Scene/SceneTransform.cpp` - fill in the orbital/plane half of the snapshot,
  apply and restore. **The header is the contract and must not change.**
- `src/Presentation/Panels/SceneOutlinerVisibilityColumns.{hpp,cpp}` - the two-column widget.
- `src/Presentation/Panels/SceneOutlinerPanel.cpp` - only if the column layout forces it.
- `src/Presentation/Panels/ObjectPropertiesPanelSections.cpp`,
  `src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp` - multi-selection editing.
- `src/Presentation/Panels/RendererPanelOrbitalMenu.cpp` - the Add > Orbital menu only.
- New tests under `tests/` for anything with logic in it.
- New icon PNGs under `install/app/assets/icons/` if you add any (generate with Pillow; look at
  the existing `tool-*.png` files for size and style).

## Files that must NOT be touched

- `src/Renderer/Scene/SceneTransform.hpp` - the contract for item 1.
- `tests/Renderer/SceneTransformTests.cpp` - the six `SceneObjectGizmoTests` cases are the
  contract. Make them pass; do not edit them.
- `src/Renderer/RendererWindowState.hpp` - the frozen scene-object model.
- Anything under `src/Domain/`, `src/IO/`, `src/App/`, `src/ScientificRuntime/`.
- `src/Presentation/Panels/ViewportScenePlaneInteraction.cpp`,
  `src/Presentation/Panels/ViewportPicking.cpp`, `src/Presentation/Panels/RendererPanel.cpp` -
  uncommitted work from the same branch, already finished.

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=SceneObjectGizmoTests.*` - all six pass. They are already
   written; they fail today because `CaptureSceneTransformSelection`,
   `ApplySceneTransformSelection` and `RestoreSceneTransformSelection` ignore
   `snapshot.orbitals` and `snapshot.planes`.
2. `SceneTransformPivotPositions` includes an orbital's centre(s) and a plane's centre, so the
   gizmo has something to draw itself around; `HasSceneObjectTransformTargets` is true for a
   selection of only orbitals or only planes.
3. The outliner's eye/camera controls are right-aligned icon buttons. A mixed group (some children
   hidden) still reads as mixed - `SceneVisibilityColumnState::visibleMixed` /
   `renderableMixed` already carry that, keep honouring them. Existing
   `SceneVisibility`/outliner tests stay green.
4. With two or more orbitals selected, the properties panel edits the shared fields (shell,
   effective charge, iso fraction, resolution, scale, colours) across all of them, one undo
   snapshot per logical edit - the same shape `DrawSelectedSceneArrowProperties` already uses for
   multi-selected arrows. Same for planes.
5. `Add > Orbital` no longer presents one long flat list.
6. Full Release test suite green: 2 skipped tests are expected
   (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
   `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`) - that is the
   `DS_PYTHON_CAPI_AVAILABLE=0` skip count, not a regression.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer/Scene/SceneTransform.cpp` is renderer-side
  geometry with no ImGui in it; the panels are `Presentation` and collect intent only.
- No exceptions in rendering paths.
- `.cpp` files stay under ~500 lines - split rather than grow.
- Do not create a parallel system next to one that already exists. `DrawSelectedSceneArrowProperties`
  (multi-selection), `SceneVisibilityColumnState` (mixed-state columns) and `ApplyTransformDelta`
  (the shared spatial maths) all exist; extend them.
- Rotating a plane must keep `normal` and `tangent` unit length and perpendicular - the renderer
  and `PickScenePlane` both assume an orthonormal frame.
- An orbital anchored to atoms has its centres rewritten from those atoms every frame by
  `ResolveAnchoredOrbitals`. Translating one therefore has to clear `anchorAtoms`, or the drag is
  undone on the next frame. The contract test pins this down.
