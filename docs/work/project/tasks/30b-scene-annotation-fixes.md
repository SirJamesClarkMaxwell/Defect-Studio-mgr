# Task 30b: scene annotation fixes (scale, dashed lines, atom buffer, outliner multi-select)

> **Status 2026-09-18: item 3 is NOT fixed.** The implemented change - moving the `Bufor` control
> above the action that reads it - was the wrong cause. The user re-tested and the buffer still does
> not open a visible gap. Items 1, 2 and 4 were implemented and their tests pass; the build is green
> (616 tests, 614 pass, 2 expected skips).
>
> Next time item 3 is picked up, do NOT trust the earlier diagnosis. What is known: the maths in
> `MatchSceneArrowPositionToAtoms` is unit-tested and correct in isolation, so the failure is in the
> live path. Things to actually check before changing anything:
> - Is `RendererAtomData::radius` the radius the sphere is *drawn* at, or a base value the renderer
>   scales afterwards (VESTA-style atom size factor, style/appearance multipliers)? A buffer computed
>   from an unscaled radius would be invisible against a sphere drawn larger.
> - Does the path the user actually used (Add > Rysuj, the vertical toolbar, the properties panel's
>   "Match position") reach `MatchSceneArrowPositionToAtoms` at all, or does something re-snap the
>   endpoints to the atom centres afterwards - an anchor resync, a snapshot restore, or the arrow's
>   own atom-attachment?
> - Watch the stored `arrow.start`/`arrow.end` after the action, not the picture. If the values carry
>   the gap and the drawing does not, the bug is in the renderer, not here.
>
> **Item 5, added 2026-09-18, not started: deleting a scene object in the viewport leaves its row in
> the Scene Outliner.** Reported for orbitals and arrows; assume every scene-object kind until proven
> otherwise, because the shape of the bug is shared. Verified while writing this down, not fixed:
> `SceneOutlinerRows.cpp` does not have one source of truth for its rows. Free labels, pinned
> measurements and arrows are listed from `windowState.sceneRegistry` via `CollectSourceIndices(...,
> SceneObjectKind::...)` (lines ~239/245/296), while orbitals are listed straight off the
> `windowState.sceneOrbitals` vector (~311-336). Deletion meanwhile erases the vectors
> (`ObjectPropertiesPanelOrbital.cpp:219`, `SceneArrowOperations.cpp:35`) and there is no
> corresponding `SceneRegistry` destroy call anywhere in `src/` - grep for `DestroyObject`/
> `RemoveObject` returns nothing.
>
> So the fix is not a per-panel patch: either the registry becomes the single source the outliner and
> the viewport both read and deletion goes through it, or the vectors do. Pick one, delete the other
> path, and add a test that deletes one object of each kind and asserts the outliner row count and the
> registry entity count both drop. A guard added to the orbital path alone will leave arrows, labels,
> measurements and planes broken in exactly the same way.

Four independent defects found while testing the task 30 remediation branch. They share no code,
so they can be done in any order, but they ship as one branch.

## Goal

1. `S` on a selected scene arrow or line scales its **geometry** (both endpoints about the pivot),
   with `x`/`y`/`z` constraining the axis exactly as for every other scene object. Today it scales
   `style.shaftWidth`/`headWidth`/`headLength` instead, so `S`+`z` on a free line visibly changes
   thickness and nothing else. Thickness stays editable in the properties panel, where it belongs.
2. A line or arrow can be drawn **dashed**, as a style flag on the object, persisted with it.
3. The `Bufor` parameter actually opens a visible gap between an arrow's ends and the two atoms it
   was matched to. The user set it and saw no gap - diagnose before changing anything, the maths in
   `MatchSceneArrowPositionToAtoms` may already be right and the bug be in where or when it is
   applied.
4. `Shift` and `Ctrl` modify the existing Scene Outliner selection: `Ctrl`+click toggles one row,
   `Shift`+click selects the range from the last clicked row to this one. Plain click still
   replaces the selection.

## Files to create or change

- `src/Renderer/Scene/SceneTransform.cpp` / `.hpp` - item 1, arrow branch of the scale operation.
- `src/Renderer/RendererWindowState.hpp` - item 2, three new fields on `ArrowStyle` (see Constraints).
- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` (or the arrow renderer it delegates to) - item 2,
  drawing the dashes.
- `src/IO/SceneObjectsYaml.cpp`, `src/IO/SceneObjectsIO.hpp`, `src/Renderer/Scene/SceneObjectPersistence.cpp`
  - item 2, round-tripping the new fields.
- `src/Presentation/Panels/SceneArrowEditorWidget.*` / `SceneArrowOperations.cpp` - item 2's controls,
  item 3's fix if it turns out to be in the UI path.
- `src/Presentation/Panels/SceneOutlinerRows.cpp`, `SceneOutlinerPanel.*` - item 4.
- `tests/` - new cases beside the existing ones for each item (see Acceptance criteria).

## Files that must NOT be touched

- Anything under `src/Domain/` - none of these four defects is domain logic.
- `src/Presentation/Panels/RendererPanelOrbitalMenu.cpp` and `src/Domain/Electronic/OrbitalPresets.cpp`
  - just rewritten in the previous commit, unrelated to these items.
- `src/App/` - no composition-root change is needed for any of this.
- `premake5.lua` - no new project, and no new build flag.

## Acceptance criteria

1. A GoogleTest in `tests/Renderer/Scene/` drives a scale operation on an arrow with an axis
   constraint and asserts the endpoints moved along that axis about the pivot while
   `style.shaftWidth`, `style.headWidth` and `style.headLength` are unchanged. Cancelling the modal
   restores the original endpoints (there is an existing cancel/restore test to extend).
2. A GoogleTest round-trips a dashed arrow through `tests/IO/SceneObjectsIOTests.cpp`: written,
   read back, `dashed`/`dashLength`/`gapLength` equal. A file written **before** this change (no
   such keys) still loads, with `dashed == false`.
3. A GoogleTest pins the gap: with `radiusBuffer > 0` and two atoms of known radius and separation,
   `arrow.start` is strictly further from `startAtom.cartesianPosition` than it was with
   `radiusBuffer == 0`, and the arrow does not invert. Then state in your report **where** the live
   bug actually was, since the maths alone may pass today.
4. A GoogleTest over the pure selection helper (extract one if the logic is currently inline in the
   ImGui row - a function taking current selection + clicked id + modifier and returning the new
   selection) covers: plain click replaces, `Ctrl` toggles without dropping the rest, `Shift`
   selects the inclusive range between anchor and clicked row, `Shift` with no anchor behaves like
   a plain click.
5. `scripts/Windows/Build.bat --config Release` builds `DefectStudio` and `DefectStudioTests` with
   zero errors and zero warnings, and the suite is 609 tests with 2 skipped.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. In particular item 4's selection helper is
  Presentation-or-Renderer logic, not Domain, and `IO` must not learn what a dash looks like - it
  only round-trips the three fields.
- New `ArrowStyle` fields, exactly these names and defaults, so persistence and the properties panel
  agree:
  ```cpp
  bool dashed = false;
  float dashLength = 0.25f; // world units, along the shaft
  float gapLength = 0.15f;  // world units
  ```
  YAML keys: `dashed`, `dash_length`, `gap_length`. Absent keys keep the defaults - old project
  files must still load.
- Dashes apply to `ArrowKind::Line` and to the shaft of `ArrowKind::Arrow3D`. An `Arrow3D` head is
  never dashed. Do not add a second arrow renderer next to the existing one; dash the geometry the
  existing path already builds.
- `.cpp` files stay under ~500 lines. `SceneOutlinerRows.cpp` and `OpenGlRendererBackend.cpp` are
  already large - extract rather than append if a change would push them further.
- Only the main thread mutates state visible in the project or UI.
- Regenerate projects with `scripts/Windows/GenerateProjects.bat` if you add any `.cpp`/`.hpp`.
- Do not build or run anything that needs an approval step; report what you changed and the session
  that dispatched you will build, run the suite and exercise the app.
