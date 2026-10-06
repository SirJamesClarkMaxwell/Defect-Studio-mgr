# Task 41 S10: picking and handle geometry

## Goal
A cursor position in a viewport can be turned into "which part of this path did I hit". Node and
bezier-handle markers get one definition that both the future Edit Mode overlay and the hit test
read, so a handle can never be drawn somewhere it cannot be clicked. Picking consumes the same
`EvaluatedPath` and the same decoration contours the renderer meshed, so what is pickable is what is
on screen.

## Files to create or change
- `src/Renderer/Path/PathHandleGeometry.cpp` (new) - implements the header, already written.
- `src/Renderer/Path/PathPicking.cpp` (new) - implements the header, already written.
- `tests/Renderer/Path/PathHandleGeometryTests.cpp` (new).
- `tests/Renderer/Path/PathPickingTests.cpp` (new).

## Files that must NOT be touched
- `src/Renderer/Path/PathHandleGeometry.hpp` and `src/Renderer/Path/PathPicking.hpp` - the contract.
  If a signature is wrong, stop and say so instead of changing it.
- Everything else under `src/Renderer/Path/` - S1-S8 are frozen. Read it, do not edit it.
- `src/Renderer/Scene/SelectionHitTest.{hpp,cpp}` - reuse it as it is. It already has
  `ProjectToScreen`, `PointInCircle` and `DistancePointToSegment`; if one of them is not quite what
  you need, write the difference locally in the new `.cpp`, do not widen the shared header.
- `src/Presentation/**`, `src/App/**`, `src/IO/**`, `src/Domain/**`, `src/Renderer/OpenGl/**`.
- Any existing test file.

## Out of scope (deliberate, do not add)
- **Any Presentation wiring.** The plan sketched a hook into `ViewportPicking.cpp`, but there is no
  `selectedScenePaths` on `RendererWindowState` and no path UI to select into: S11 owns the selection
  vector, the click handler and the Outliner row, and wiring a picker into a window that cannot store
  the answer is code with no caller. S10 delivers the library S11 calls.
- Drawing the markers. S12 owns the overlay; this stage only defines where they are and how big.
- Region select (rect/circle) over paths. S11 lists it with the rest of the Object Mode surfaces.
- Arbitration between two paths, or between a path and an atom. The result carries `worldPosition`
  and `screenDistance` so the caller can arbitrate; deciding a winner needs the other objects'
  candidates, which only the caller has.

## Acceptance criteria
### PathHandleGeometry
1. A three-node path with one Line and one Cubic segment yields five markers: three `Node` markers in
   node order, then the Cubic's start handle and end handle in that order.
2. A path of Line and Arc segments only yields exactly one marker per node and no `BezierHandle`.
3. A `BezierHandle` marker's `owner` is the id of the node it hangs off - the segment's start node for
   `startHandle`, the end node for `endHandle`. A `Node` marker's `owner` equals its own `element`.
4. A node whose binding resolved to a different position than its authored one gets its marker at the
   resolved position (feed `ResolvedNodes::positions` that differs from `path.nodes[i].position`).
5. An element behind the camera produces no marker, and the remaining markers are unaffected.
6. `activeElement` matching a node gives exactly that marker `kPathActiveHandleDrawRadius` /
   `kPathActiveHandlePickRadius`; every other marker keeps the plain radii. An unset `activeElement`
   enlarges nothing.
7. A non-finite node position, a zero or negative viewport size, or a non-finite matrix yields an
   empty vector and no NaN.
8. `ProjectWorldRadiusToPixels` on a point in front of the camera returns a positive, finite value
   that grows as the point comes closer; the same point behind the camera returns `nullopt`; a
   non-finite radius or a degenerate viewport returns `nullopt`.

### PathPicking
9. A path with `visible == false`, and one with `renderable == false`, both return `None` for a cursor
   sitting exactly on the stroke.
10. Render/pick parity: for a straight path drawn at a known width, a cursor at the centreline hits
    with `screenDistance == 0`; a cursor offset by (projected half-width + `kPathStrokePickTolerance`
    - 1px) hits; one offset by (projected half-width + `kPathStrokePickTolerance` + 1px) does not.
11. A cursor past the end of the stroke, beyond the cap, does not hit the segment.
12. In `editMode`, a cursor on a node returns `Node` with that node's id; on a bezier handle it
    returns `Handle` with the handle's id.
13. Arbitration: a cursor placed where a handle, a node and the stroke all pass their tolerance
    returns `Handle`; with the handle removed from the path it returns `Node`; with both gone it
    returns `Segment`.
14. Arbitration: a cursor inside an endpoint decoration and also within stroke tolerance returns
    `Decoration`, with `element` equal to the endpoint node's id.
15. A `PathDecorationKind::None` decoration is never a candidate - the same cursor returns `Segment`.
16. With `editMode == false`, a cursor on a node returns `WholePath` with an unset `element`, and a
    cursor on the stroke returns `WholePath`. Nodes and handles are never returned in Object Mode.
17. A handle that projects behind the camera is not pickable, and the cursor at its (stale) screen
    position falls through to whatever is actually there.
18. `activeElement` widens only that element's radius: a cursor 23px from the active node hits, the
    same offset from a non-active node does not.
19. `PickPath` with `evaluated.samples` empty still picks nodes, handles and decorations, and returns
    `None` for a cursor out in empty space.
20. A dashed style picks along its whole span: a cursor in a dash gap still returns `Segment`.
21. Non-finite settings (cursor, matrix, `cameraRight`) or a degenerate viewport return `None` with
    `screenDistance == 0` - never a NaN.
22. `result.worldPosition` on a segment hit lies on the evaluated polyline, within a sample spacing of
    the true nearest point.
23. The full Release suite is green with the two permanent `DS_PYTHON_CAPI_AVAILABLE=0` skips and no
    other skips.

## Constraints
- `Renderer` is the exception-free zone: no `throw`. Nothing here needs an error channel - the result
  type already carries "no hit".
- No ImGui, no GL, no `RendererWindowState`, no `RendererViewCamera` in either new `.cpp`. A matrix, a
  viewport size and a cursor are the whole input, and that is what makes the tests run without a
  context.
- Reuse `SelectionHitTest::ProjectToScreen`, `PointInCircle` and `DistancePointToSegment` rather than
  writing screen-space math again. Reuse `BuildDecorationContour` and `TrimmedRange` from
  `PathDecoration.hpp` for the decoration hitbox - the contour is the geometry the mesher used.
- `.cpp` files stay under ~500 lines.
- `glm::isfinite` / `glm::all` are unavailable - check finiteness component-wise with `std::isfinite`.
  GLM constructors are explicit.
- Tests build an `EvaluatedPath` by hand where that is simpler (see `PathStrokeMesherTests.cpp`'s
  `StraightPath`/`Sample` helpers for the established shape) and go through `Tessellate` where the
  real sample distribution matters.
- Run `scripts/Windows/GenerateProjects.bat` after adding the new files. Do not build - the sandbox
  cannot build this project; this session builds and tests.

## Manual round (user, after the stage)
None. S10 ships no reachable behaviour - nothing in the UI calls it until S11. The first picking
manual round is S11's.
