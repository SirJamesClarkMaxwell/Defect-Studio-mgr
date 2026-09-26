# Task 41 S11m: a Flat ribbon can have thickness

## Goal

A `Flat` stroke is a zero-thickness sheet: seen edge-on it disappears. That is right for a diagram
drawn on a plane and wrong for a curved arrow meant to read as an object - the shape the manual
round asked for, a 2D arrow bent through 3D with visible depth.

After this task a Flat stroke has a `ribbonThickness`. Zero keeps today's sheet. A positive value
extrudes the ribbon along its own normal, so its cross-section is a `width` by `thickness`
rectangle, and the arrowhead is a solid of the same thickness rather than a sheet glued to a solid.

**This is a setting, not a new profile.** Do not add a `StrokeProfile` enumerator, and do not build
a parallel mesher beside the existing one.

## The contract, already written

- `src/Renderer/Path/PathStyle.hpp` - `PathStrokeStyle::ribbonThickness`, default 0. Ignored by
  Round and by CameraFacing, and the comment says why.
- `src/Renderer/Path/PathStrokeMesher.hpp` - the S11m bullet in the `BuildStroke` contract. Read it
  before writing anything; it states the ring construction and what must NOT grow a special case.
- `src/IO/SceneObjectsIO.hpp` - `PersistedScenePathStyle::ribbonThickness`, additive, absent means
  0, format version stays 2.
- `src/Presentation/Panels/ScenePathEditorWidget.hpp` - `ScenePathStyleEdit::ribbonThickness` and
  `ScenePathStyleEditState::mixedRibbonThickness`.

The design in one line: a thick Flat stroke is built the way Round is built - rings stitched to
their neighbours, into `tubeVertices` - with a four-corner rectangular ring in place of Round's
circular one, at
`sample.position +/- (width/2) * normal +/- (thickness/2) * binormal`.

Everything downstream already works on rings. The joins, the caps, the decoration meshing and the
S11h decoration/shaft handoff must need no special case for thickness; if you find yourself adding
one, the ring generator is in the wrong place.

## Files to create or change

- `src/Renderer/Path/PathStrokeMesher.cpp` - route a thick Flat stroke through the ring path, and
  make the ring generator depend on the cross-section rather than on the profile
- `src/Renderer/Path/PathDecorationMesher.cpp` - the decoration's rings, the same way
- `src/Renderer/OpenGl/OpenGlPathRenderer.cpp` - **lines 180 and 188 decide `tube` from
  `style.profile == StrokeProfile::Round`. That is no longer true.** Key off which vertex array
  `BuildStroke` populated instead.
- `src/Renderer/Scene/ScenePathPersistence.cpp` and `src/IO/SceneObjectsYaml.cpp` - the round trip
- `src/Presentation/Panels/ScenePathEditorWidget.cpp` - a thickness drag, shown for a Flat
  selection the same way Ribbon normal already is, and using the S11g
  `BeginScenePathStyleDrag` / `CommitScenePathStyleDrag` pair
- `src/Presentation/Panels/ScenePathDevMenu.cpp` - a dev preset that makes the shape the request
  came from: a curved Flat ribbon with thickness and an arrowhead

## Files that must NOT be touched

- The four headers listed above - they are the contract
- `src/Renderer/Path/PathTessellator.cpp`, `PathFrames.cpp` - the frames are what thickness is
  measured along; they do not change
- `src/Renderer/OpenGl/Shaders/` - a thick ribbon is real geometry and goes through the existing
  tube shader. No shader change.
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. `ribbonThickness == 0` on a Flat stroke produces exactly the geometry it produces today:
   populated `ribbonVertices`, empty `tubeVertices`, byte-identical output. This is the regression
   guard and it covers every existing Flat test.
2. `ribbonThickness > 0` on a Flat stroke populates `tubeVertices` and leaves `ribbonVertices`
   empty. Exactly one of the two is populated, as the contract has always said.
3. Each ring of a thick Flat stroke has exactly four vertices, whatever `style.radialSegments`
   says.
4. Those four corners are at `position +/- (width/2) * normal +/- (thickness/2) * binormal`:
   assert the two spans by projecting onto `normal` and `binormal`, not by pinning coordinates.
5. A thick Flat stroke with an end decoration extrudes the decoration to the same thickness.
6. The S11h handoff still holds for a thick Flat stroke on a curved path: the shaft's boundary sits
   at the decoration's back position with the endpoint's frame.
7. `ribbonThickness` is ignored by Round and by CameraFacing - their geometry does not change when
   it is set.
8. A negative or non-finite thickness is treated as zero rather than producing inverted geometry.
9. `ribbonThickness` round-trips through save and load; a v2 file without the key loads as 0 and
   the format version stays 2.
10. The Properties panel shows a thickness control for a Flat selection, and dragging it is one
    undo entry.
11. The dev menu offers the curved thick ribbon preset.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` stays exception-free.
- `.cpp` files stay under ~500 lines. `PathStrokeMesher.cpp` is 274 and
  `PathDecorationMesher.cpp` exists precisely so this kind of work has somewhere to go.
- `BuildStroke` must stay camera-independent.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- If a test outside `tests/Renderer/Path/` breaks because it assumed profile implies vertex array,
  report it - do not fix it, the test session does.
