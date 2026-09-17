# Task 29: draw a line, an arrow or a plane through what is selected

## Goal

Select two atoms and get a line or an arrow between them; select three and get the plane they lie
in. The result is a plain drawing object at those world positions - not anchored to the atoms, not
called a bond, not something that has to be unlinked later. This replaces the abandoned task 26a,
whose whole premise (a scene arrow that *is* a bond, with a bond order and a permanent link to two
atoms) was the part that was not wanted.

## Files to create or change

- `src/Renderer/Scene/ScenePlaneGeometry.cpp` - new, implements the three functions in the header
  of the same name. Pure geometry, no ImGui, no GL.
- `src/Presentation/Panels/RendererPanel.cpp` - in the viewport Add menu, a `Rysuj` submenu with
  `Linia`, `Strzalka` and `Plaszczyzna`, enabled when enough atoms are selected (two for line and
  arrow, one or more for a plane - `FitScenePlane` handles the under-determined cases itself).
  Line and arrow write a `SceneArrow` with `start`/`end` copied from the selected atoms' current
  positions; plane writes `MakeScenePlane(*FitScenePlane(positions, cameraForward))`. None of them
  records which atoms were used.
- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` (plus a new
  `src/Renderer/OpenGl/OpenGlScenePlaneRenderer.cpp` if that file is already at its size limit) -
  draw each visible `ScenePlane` as a translucent two-triangle quad from `ScenePlaneCorners`, with
  `showBorder` adding its outline. Depth-test on, depth-write off, back faces not culled - a plane
  is meant to be looked at from both sides. Draw it with the other translucent geometry, after the
  opaque atoms.
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp` (or a sibling file, see the size note) - a
  "Plane" section for `selectedScenePlanes`: centre, normal, half extents, colour, alpha, border,
  and the two visibility toggles.
- `src/Presentation/Panels/SceneOutlinerPanel.cpp` - a "Planes" group beside Labels, Arrows and
  Orbitals.
- `src/IO/SceneObjectsIO.{hpp,cpp}` and `src/Renderer/Scene/SceneObjectPersistence.cpp` - a
  `PersistedScenePlane` in the same shape as the other kinds, under `kind: ScenePlane`. Unlike
  orbitals there are no atom references to resolve; a plane is just its own numbers.

## Files that must NOT be touched

- `src/Renderer/Scene/ScenePlaneGeometry.hpp` and the `ScenePlane` struct in
  `src/Renderer/RendererWindowState.hpp` - the contract. If you believe a field is missing, stop
  and say so.
- Everything under `tests/`.
- Anything under `src/Domain/`.
- `ArrowKind` must NOT gain a `Bond` member, `SceneArrow` must NOT gain an order, a dash flag or
  atom anchors. That was the abandoned design; re-introducing it is the one thing this task exists
  to avoid.

## Acceptance criteria

1. `tests/Renderer/Scene/ScenePlaneGeometryTests.cpp` passes in full - eight cases, all currently
   failing because `ScenePlaneGeometry.cpp` does not exist yet.
2. Every other existing test still passes.
3. Two atoms selected, Add > Rysuj > Linia gives a line whose ends sit at those two atoms; moving
   an atom afterwards does not move the line.
4. Three atoms selected, Add > Rysuj > Plaszczyzna gives a translucent quad containing all three.
5. Two atoms selected and a plane requested gives a quad through both that faces the camera, not an
   edge-on sliver.
6. Planes appear in the Scene Outliner, can be edited in Object Properties, and survive save/reopen.

## Constraints

- Layer boundaries in `AGENTS.md` are hard. No exceptions thrown in the rendering path.
- `.cpp` files stay under ~500 lines; `ObjectPropertiesPanel.cpp` (1125) and
  `SceneOutlinerPanel.cpp` (656) are already over, so put new sections in sibling files and re-run
  `scripts/Windows/GenerateProjects.bat`.
- `ScenePlaneGeometryTests.cpp` is a new file, so the projects must be regenerated before it builds.
- Do not create a parallel system next to one that already exists - planes are scene objects and
  reuse the scene-object selection, outliner and persistence machinery.
- Do NOT run a build or the tests - the MSBuild toolchain is not reachable from your sandbox.

## Also in this task: the colour gradient

`RendererWindowState::ArrowStyle` has gained `bool useGradient` and a `RendererColorGradient
gradient` - the same two-stop type that already colours structure bonds, not a second gradient
mechanism.

- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` - when `useGradient` is set, colour the shaft by
  interpolating `gradient.start` to `gradient.finish` along its length (start at `SceneArrow::start`,
  finish at `end`), instead of the flat `style.color`. Arrow3D's head takes `gradient.finish`. When
  it is clear, nothing changes - every existing arrow keeps its flat colour.
- The properties panel gets a checkbox and two colour pickers next to the existing colour, and the
  flat colour stays visible and editable so turning the gradient off does not lose it.
- Persist both alongside the rest of `PersistedArrowStyle`.

While you are in `ArrowStyle`: `shaftWidth` is documented as a radius but the renderer computes
`shaftRadius = 0.5f * style.shaftWidth`, so it is really a diameter. Fix the comment to match the
code - do NOT change the arithmetic, which would silently halve every arrow anyone has already
drawn.
