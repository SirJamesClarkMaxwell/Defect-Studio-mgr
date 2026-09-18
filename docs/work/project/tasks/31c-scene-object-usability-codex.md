# Task 31c: add menus, plane anchoring, outliner selection

Third slice of the scene-object usability work, from a second hands-on testing pass. The gizmo
pivot, the selection-highlight strength, the atom/annotation selection split, the outliner's
keyboard-nav flag and the side panel's auto-width are already fixed on this branch - leave them
alone. Five things are left.

## Goal

1. `Shift+A` offers the same things the viewport's right-click `Add` submenu does, through the
   same code. Today it carries a private, stale copy: a flat list of every orbital preset, no
   plane, no line, and a `MakeDefaultSceneOrbital` call that never anchors to the selected atoms.
   That copy is the whole of the bug - adding an orbital on two atoms from `Shift+A` dropped one
   unanchored orbital at the 3D cursor, while the right-click path placed them correctly.
2. A plane and a line/arrow can be added with no atoms selected, from `Shift+A` and from the
   viewport's vertical toolbar - not only through `Rysuj (N atomow)`, which needs two atoms.
3. The `Add > Orbital` menu says where the orbital will land in the items themselves. Today a
   checkable `Na zaznaczonym atomie` row above the presets is a *mode*: it has to be clicked
   before the preset, and nobody found it. The tester's words: "panele z dodawaniem orbitali sa
   bardzo niejednoznaczne, szczerze nie mam pojecia jak to dziala."
4. A plane fitted to atoms stays fitted to them while they move, and can be detached.
5. The Scene Outliner's selected row is legible.

## Files to create or change

- `src/Presentation/Panels/RendererPanelToolbar.cpp` - the `Shift+A` menu (`drawAddMenu`). Delete
  the private orbital loop; call the shared menu instead.
- `src/Presentation/Panels/RendererPanelOrbitalMenu.{hpp,cpp}` - the placement rework, plus the
  free-standing plane and line entries. Split the file if it passes ~500 lines.
- `src/Presentation/Panels/ViewportVerticalToolbar.cpp` - the add-plane and add-segment buttons.
- `src/Presentation/Panels/ViewportInteraction.cpp` - call `ResolveAnchoredScenePlanes` next to the
  existing `ResolveAnchoredOrbitals` call (line ~29).
- `src/Presentation/Panels/ObjectPropertiesPanelSections.cpp` - the plane's anchor readout and
  `Odczep` button. Copy the shape of the orbital's, in `ObjectPropertiesPanelOrbital.cpp` ~line 75.
- `src/IO/SceneObjectsIO.hpp`, `src/IO/SceneObjectsYaml.cpp` - persist `ScenePlane::anchorAtoms`.
  The orbital's `anchorAtoms` is already written there; follow it exactly.
- `src/Presentation/Panels/SceneOutlinerRows.cpp` - the selected-row styling.
- New tests under `tests/` for anything with logic in it.

## Files that must NOT be touched

- `src/Renderer/Scene/ScenePlaneGeometry.{hpp,cpp}` - `MakeDefaultScenePlane` and
  `ResolveAnchoredScenePlanes` are already written and their four tests already pass. Item 4 is
  about wiring them up - the per-frame call, the button, the persistence - not about the geometry.
- `src/Renderer/Scene/SceneTransform.hpp` - the gizmo contract.
- `tests/Renderer/Scene/ScenePlaneGeometryTests.cpp` - the four `ScenePlaneAnchorTests` cases are
  the contract for item 4. Make them pass; do not edit them. The rest of that file is already green.
- `tests/Renderer/SceneTransformTests.cpp` - already green, including the pivot and atom-exclusion
  cases added this round.
- `src/Renderer/Scene/SceneTransform.cpp`, `src/Presentation/Panels/ViewportPicking.cpp`,
  `src/Presentation/Panels/SceneOutlinerPanel.cpp`, `src/Presentation/Panels/ViewportSidePanel.cpp`,
  `src/Renderer/Scene/SceneObjectAppearance.{hpp,cpp}` - finished this round.
- Anything under `src/Domain/`, `src/App/`, `src/ScientificRuntime/`.
- `src/Renderer/RendererWindowState.hpp` - `ScenePlane::anchorAtoms` has already been added there;
  nothing else in that file changes.

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=ScenePlaneAnchorTests.*` - all four pass.
2. `grep -n "OrbitalPreset::S" src/Presentation/Panels/RendererPanelToolbar.cpp` finds nothing: the
   duplicated preset loop is gone and `Shift+A` reaches `DrawOrbitalAddMenu`.
3. From `Shift+A` with two atoms selected, a one-centre preset adds one orbital anchored to each of
   them - the same result the right-click menu already gives.
4. `Shift+A` and the vertical toolbar can both add a plane and a line with nothing selected.
5. The `Add > Orbital` items name their own placement, e.g. `Na 2 zaznaczonych atomach >` and
   `W kursorze 3D >`, each opening the grouped preset catalogue. No checkable mode row survives,
   and `anchorOrbitalToSelection` is either gone or no longer decides where a preset lands. Both
   entry points show the same menu, because there is only one.
6. Moving an atom that a plane is anchored to moves the plane. `Odczep` leaves the plane where it
   is on screen and clears `anchorAtoms` - the orbital's button does exactly this, including the
   freeze-in-place part. A saved and reloaded project keeps the anchor.
7. A selected outliner row is clearly distinguishable from an unselected one.
8. Full Release suite green: 2 skipped is the `DS_PYTHON_CAPI_AVAILABLE=0` count, not a regression.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer/Scene/` has no ImGui in it; the panels are
  `Presentation` and collect intent only.
- No exceptions in rendering paths.
- `.cpp` files stay under ~500 lines - split rather than grow.
- Do not create a parallel system next to one that already exists. This task is mostly the
  opposite: `drawAddMenu`'s private orbital list is the duplicate, and deleting it is the point.
  `DrawOrbitalAddMenu`, `DrawSegmentAddItems`, `DrawPlaneAddItem`, `MakeScenePlane`,
  `FitScenePlane` and `ResolveAnchoredOrbitals` all exist; extend and reuse them.
- `ResolveAnchoredScenePlanes` runs every frame over every plane. Keep it allocation-light and
  leave unanchored planes untouched.
- A plane's `normal` and `tangent` must stay unit length and perpendicular - the renderer and
  `PickScenePlane` both assume an orthonormal frame.
- UI strings in this codebase are unaccented Polish. Match the surrounding style.
