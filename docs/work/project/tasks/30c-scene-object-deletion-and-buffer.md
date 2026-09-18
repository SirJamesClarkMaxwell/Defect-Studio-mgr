# Task 30c: deleted objects leave the outliner, and the atom buffer opens a real gap

Two defects left over from task 30b, both re-tested by the user and still broken. They share no
code and ship as one branch: `task/30c-scene-object-deletion-and-buffer`.

Both were diagnosed before this file was written. The diagnoses below are not guesses - each one
names the lines that were read. Do not re-derive them; verify and fix.

## Item A: deleting a scene object leaves its row in the Scene Outliner

Reported for orbitals and arrows, and it is the same bug for planes.

**Root cause, confirmed.** `SceneSystem::SyncLabelEntities` (`src/Renderer/Scene/SceneSystem.cpp:224`)
destroys every label/arrow/free-label/orbital entity and rebuilds the whole set from
`windowState.pinnedMeasurements` / `freeLabels` / `sceneArrows` / `sceneOrbitals`. So the vectors are
already the single source of truth and the registry is a mirror - the mirror is simply never
refreshed after a delete. The three erase functions drop the element and return:

- `EraseSceneOrbitals` - `src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp:210`
- `EraseScenePlanes` - `src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp:223`
- `EraseSceneArrows` - `src/Presentation/Panels/SceneArrowOperations.cpp:22`

None of them calls `SceneSystem::SyncLabelEntities`. The registry keeps the destroyed object's
entity, the outliner lists it, and every surviving object of that kind now carries a stale
`SceneObjectComponent::sourceIndex`, because erasing from the middle of a vector shifts everything
after it. That second half matters as much as the ghost row: after deleting object 2 of 5, rows 3-5
point at the wrong objects.

**The fix is a resync at the end of each erase**, not a per-panel guard and not an architecture
change. Presentation already calls `SyncLabelEntities` directly in
`ViewportLabelInteraction.cpp:71,120` and `RendererPanelOrbitalMenu.cpp:263`, so the precedent and
the layer boundary are settled.

Two things to check while you are in there, because they are the same bug wearing a different hat:

- `SyncLabelEntities` rebuilds four kinds and **not** `scenePlanes` - `SceneRegistry` has no
  `PlaneEntities()`. Find out how plane rows reach the outliner. If planes are listed from the
  vector directly, deleting one cannot leave a ghost and `EraseScenePlanes` needs no resync; say so
  in your report. If they go through the registry by some other route, fix that route too.
- Any other place that erases from one of these vectors (undo/redo restore, project load, clear
  scene, cut/paste) has the same requirement. Grep for `.erase(` and `.clear()` on
  `sceneArrows`/`sceneOrbitals`/`freeLabels`/`pinnedMeasurements`/`scenePlanes` across `src/` and
  make sure every one of them ends with the registry in sync. Where the erase is already followed by
  a resync higher up the call stack, leave it alone and list it in your report - a second resync per
  frame is waste, not safety.

## Item B: the `Bufor` parameter does not open a visible gap

**What has already been ruled out** (do not spend time re-checking these):

- The maths in `MatchSceneArrowPositionToAtoms` (`SceneArrowOperations.cpp:134-158`) is correct and
  unit-tested.
- `RendererAtomData::radius` **is** the radius the sphere is drawn at:
  `StructureRendererDataBuilder.cpp:91` sets it from `atomStyleTable.DisplayRadius(element)` and
  `OpenGlRendererBackend.cpp:1863` passes exactly that value as the sphere instance radius. Nothing
  scales it in between.
- `SceneArrow` has no atom attachment and nothing re-snaps `start`/`end` after the match - the
  struct holds plain coordinates (`RendererWindowState.hpp`, `struct SceneArrow`).

**The remaining lead, and the one that matches what the user saw.** The buffer is a session-wide
static (`GetSceneArrowAtomBuffer`, `SceneArrowOperations.cpp:125`) whose default is `1.0`, and the
comment on it says `1.0 = start exactly at the drawn sphere`. Its only control,
`DrawSceneArrowAtomBufferControl`, is drawn in the properties panel beside the `Match position`
button. But the path the user actually draws with - `DrawSegmentAddItems` in
`RendererPanelOrbitalMenu.cpp:156-175`, the `Add > Rysuj` items - calls
`MatchSceneArrowPositionToAtoms(..., GetSceneArrowAtomBuffer())` **without ever showing that
control**. So a freshly drawn arrow always uses `1.0`, lands tangent to the two spheres, and looks
exactly like an arrow that ignores the buffer. A user who then raises `Bufor` in the properties
panel changes nothing about the arrow already on screen, because that value is only read when
`Match position` is pressed.

Verify that story before fixing it - print or assert the stored `arrow.start`/`arrow.end` against
the atom centres for a drawn arrow and confirm the gap is `1.0 * radius` and not zero. If the values
do carry a gap, then Item B is a rendering bug (the shaft is drawn from the centre regardless of
`arrow.start`) and you should say so and fix it there instead.

Assuming the story holds, the fix has two halves and both are required:

1. Show `DrawSceneArrowAtomBufferControl()` in the `Add > Rysuj` menu, above the segment items that
   read it, so the value that will be used is visible and editable at the moment it is used. A
   setting that is only reachable from a different panel than the action it governs is the defect.
2. `1.0` meaning "tangent to the sphere" is a bad default for a control called `Bufor` whose tooltip
   promises an *odstep*. Keep the unit (atom radii) and keep `0` meaning centre-to-centre, but make
   the default `1.15` so a freshly drawn arrow visibly clears the sphere. Update the comment on the
   static and the tooltip so they say what the number now means.

## Files to create or change

- `src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp` - item A, `EraseSceneOrbitals` /
  `EraseScenePlanes`.
- `src/Presentation/Panels/SceneArrowOperations.cpp` - item A, `EraseSceneArrows`; item B, the
  static's default and its comment, and the tooltip text.
- `src/Presentation/Panels/RendererPanelOrbitalMenu.cpp` - item B, drawing the buffer control in the
  `Add > Rysuj` menu.
- Whatever other erase sites the grep in item A turns up.
- `tests/` - new cases, see Acceptance criteria.

## Files that must NOT be touched

- `src/Domain/` - neither defect is domain logic.
- `src/App/` - no composition-root change is needed.
- `premake5.lua` - no new project and no new build flag. (Still run
  `scripts/Windows/GenerateProjects.bat` if you add a `.cpp`/`.hpp`.)
- `src/Renderer/Scene/SceneRegistry.hpp` - the registry API is sufficient as it stands. If you
  believe it is not, stop and say so rather than adding a method.
- `src/Renderer/Scene/SceneSystem.cpp` - unless the plane question in item A forces it, and then say
  in your report why.

## Acceptance criteria

1. A GoogleTest builds a `RendererWindowState` with three scene arrows, erases the middle one, and
   asserts: the registry holds two arrow entities, the surviving arrows' `SceneObjectComponent`
   `sourceIndex` values are `0` and `1`, and `FindObject` on the deleted id returns an invalid
   entity. Same test shape for orbitals. If planes turn out to bypass the registry, assert that
   instead and say so.
2. A GoogleTest drives the outliner row collection after that same erase and asserts the row count
   dropped by one and no row names the deleted object.
3. A GoogleTest pins the drawn-arrow gap: with the default buffer, an arrow matched to two atoms of
   known radius and separation has `glm::length(arrow.start - startAtom.cartesianPosition)` strictly
   greater than `startAtom.radius`, i.e. the arrow clears the sphere rather than touching it.
4. `scripts/Windows/Build.bat --config Release` builds `DefectStudio` and `DefectStudioTests` with
   zero errors and zero warnings. Expect 2 skipped tests (`DS_PYTHON_CAPI_AVAILABLE=0`) - that is
   not a regression.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. Presentation may call `SceneSystem::SyncLabelEntities`
  (there is precedent, cited above); it may not reach into `entt` directly.
- Only the main thread mutates state visible in the project or UI.
- `.cpp` files stay under ~500 lines. Extract rather than append if a change would push one further.
- Do not build a second deletion path beside the resync. One call at the end of each erase, or one
  small helper that every erase ends with - not a new manager, not a new event.
- Do not build or run anything that needs an approval step. Report what you changed; the session
  that dispatched you will build, run the suite and exercise the app by hand.
