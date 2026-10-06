# Task 41 transform-1d: the gizmo stands on the path, and the axis line stays in the viewport

Two defects from the manual round, both visible in one screenshot of the decoration gallery.

## Defect A - the gizmo is at the world origin

`SceneTransformPivotPositions` takes a path's pivot from `path.transform.position`
(`src/Renderer/Scene/SceneTransform.cpp:230`). For any path that has not been moved that is
`(0,0,0)`, because the geometry lives in the node positions and the transform starts as the
identity. So the gizmo appears at the world origin instead of on the selected path.

Every other kind in that function contributes a position that is actually somewhere: an arrow's
endpoints, a plane's centre, an orbital's centre. A path must contribute its **resolved geometry**,
not its transform's translation.

Use the resolved node positions - `ResolveNodePositions` with the window's binding context, the
same way anything else that needs a path in world space does - and contribute those. Every node,
not a centroid: `SceneTransformPivotPositions` returns a list and the caller reduces it according
to `TransformPivotMode`, so handing it one averaged point would break Median and Bounding-box mode
in a different way.

This is a regression from transform-1: node positions became local and this call site still read
them as if nothing had changed. **Check for others.** Any code that needs a path in world space and
reads `node.position` directly is now wrong. The handle rules and the evaluator are NOT - they work
entirely in local space and are consistent - so look for consumers that mix a path with something
else in the scene: picking, bounds, framing, the outliner, snapping.

## Defect B - the constraint axis line spans the whole window

Pressing `X` during a modal transform draws a red line across the entire application window,
through the Outliner and the Properties panel, instead of being confined to the viewport.
`DrawConstraintLine` is at `src/Presentation/Panels/ViewportModalTransform.cpp:93-120`; the line is
drawn into a draw list without being clipped to the viewport's rectangle.

Clip it. The viewport's screen rect is known at the call site - the same rect the side panel
publishes for S11l is derived from it - so this is a clip-rect push around the drawing, not new
geometry.

## Files to create or change

- `src/Renderer/Scene/SceneTransform.cpp` - the pivot
- `src/Presentation/Panels/ViewportModalTransform.cpp` - the clip
- whatever the sweep for Defect A turns up

## Files that must NOT be touched

- `src/Renderer/Path/PathTypes.hpp`, `PathBindingResolver.hpp` - the contracts
- `src/Renderer/Path/PathStrokeMesher.cpp`, `PathDecorationMesher.cpp`
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. A selected path's pivot positions are its resolved node positions in world space.
2. A path with a non-identity transform contributes positions that reflect it.
3. A path with bound nodes contributes the bound nodes' world positions, since that is where they
   actually are.
4. Median, Bounding box and Individual pivot modes each behave as they do for arrows and planes -
   the list is per-node, not pre-reduced.
5. No other site reads `node.position` as a world position. Report what the sweep found, with
   `file:line`, even where you changed nothing.
6. The constraint line is clipped to the viewport rect and does not draw over any panel.
7. The line still appears for a path-only selection, which is what the manual round was checking.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` stays exception-free.
- `.cpp` files stay under ~500 lines.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
  Another session is working on persistence at the same time.
