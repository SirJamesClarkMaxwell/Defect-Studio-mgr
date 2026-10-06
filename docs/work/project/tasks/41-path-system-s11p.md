# Task 41 S11p: a thick arrowhead is not open at both ends

## Goal

The manual round photographed a curved thick Flat ribbon with an Arrow tip. Where the shaft meets
the head there is a dark crease that reads as a notch cut into the head. It is not shading and it
is not a T-junction step: it is a **hole**, and you are seeing the inside of the arrowhead.

## The cause

`BuildDecorationContour` gives Arrow the contour `(0,0) -> (length,width) -> (length,0)`
(`PathDecoration.cpp:60-64`). Both ends have `halfWidth == 0`.

For a **circular** cross-section that closes the solid: the ring collapses to a single point on the
axis, so the band stitched to its neighbour is a fan, and the cone is sealed.

For the **rectangular** cross-section of a thick Flat stroke it does not. A ring of half-width 0
still has thickness, so it is a line segment, not a point: its four corners are
`(0, +t/2)` twice and `(0, -t/2)` twice. The quad band to the wider neighbouring ring degenerates on
two of its four sides and leaves a triangular hole on each. An Arrow therefore has a hole at the
tip and a hole at the back, and the back one is what the screenshot shows.

The relevant code is `AppendFilledDecoration` in `src/Renderer/Path/PathDecorationMesher.cpp`,
which emits the back fan only when `contour.points.back().halfWidth > 0.0` - a condition that is
right for a circle and wrong for everything else. The hollow variant has the same shape of problem.

The contract is now stated above `UsesTubeVertices` in
`src/Renderer/Path/PathDecorationMesher.hpp`: a degenerate ring must be closed explicitly, at both
ends, for every non-circular cross-section.

## Files to create or change

- `src/Renderer/Path/PathDecorationMesher.cpp` - close the degenerate rings

## Files that must NOT be touched

- `src/Renderer/Path/PathDecorationMesher.hpp` - the contract
- `src/Renderer/Path/PathDecoration.cpp` - the contour is correct; a shape that tapers to a point
  is a shape that tapers to a point, and the mesher is what has to cope
- `src/Renderer/Path/PathStrokeMesher.cpp` - the shaft is not involved
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. A thick Flat stroke with an Arrow end decoration produces a closed decoration: every edge of its
   triangle set is shared by exactly two triangles. This is the criterion; the rest are
   consequences.
2. The same holds at the START of the stroke, not only the end.
3. The same holds for every kind whose contour reaches `halfWidth == 0` at either end - Arrow,
   Stealth, Diamond, Kite, Circle - at a thickness greater than zero.
4. A kind whose contour never reaches zero - Square, Bar, Latex - is unaffected, and its triangle
   count does not change.
5. `Round` output is byte-identical to before this task. The circular case already closed
   correctly and must not be re-plumbed.
6. A zero-thickness Flat stroke is byte-identical to before: it goes through the ribbon path and
   never sees a ring.
7. The hollow case (`filled == false`) is closed too: an outline has an inner wall and an outer
   wall, and the band between them at a degenerate end needs the same treatment.
8. No degenerate triangles are emitted - no triangle with two identical vertex positions - where
   the fix replaces a collapsed band.
9. Every index stays in bounds and the decoration's range still does not overlap the shaft's.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` stays exception-free.
- `.cpp` files stay under ~500 lines.
- Winding must stay consistent: the closing face at the tip faces forward, the one at the back
  faces backward. A face wound the wrong way is invisible from outside and looks exactly like the
  hole it was meant to fix, so it would pass a naive eyeball check.
- Do not build, do not run tests, do not commit. The verifying session does all three.

## A note for whoever reads the screenshot again afterwards

The user also reported the edges as "very sharp" and asked for a bevel. A chamfer is a separate,
larger task: the diagnosis established that the corner normals are already shared and
smooth-shaded, so the sharpness is the silhouette, not the shading. Fix the holes first - a dark
open gap along an edge reads as a hard edge, and the two complaints may be one.
