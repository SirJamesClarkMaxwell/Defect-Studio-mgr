# Task 41 S11h: the arrowhead and the shaft meet at the same place

## Goal

On a curved path the endpoint decoration looks detached: on a Flat ribbon and identically on
CameraFacing the arrowhead reads as "glued to some plane of its own", and on a Round tube the join
shows a notch. After this task the shaft and the decoration share one boundary, so the head sits on
the end of the shaft at every curvature and in all three profiles.

## Diagnosis, already done

A read-only pass established the cause; do not re-derive it, but do check the line numbers still
say what this claims before you change anything.

`BuildStroke` emits the decorations and the shaft as two independent objects
(`PathStrokeMesher.cpp:295-315`):

- The decoration is a straight extrusion from the endpoint's own frame - `endpoint.position`,
  `endpoint.tangent`, `endpoint.normal`, `endpoint.binormal` (`PathStrokeMesher.cpp:201-207`,
  `214-216`, `239-242`).
- The shaft is trimmed by arc length (`TrimmedRange`, `PathDecoration.cpp:64-70`) and its last
  sample comes from `AtLength` (`PathStrokeMesher.cpp:43-77`), which samples the real curve and its
  transported frame.

On a straight path those coincide. On an `Arc` or a `Cubic` they do not: the decoration's back
plane sits on the endpoint tangent while the shaft's end sits on the curve, with a frame that has
transported away from the endpoint's. There is no shared boundary vertex between the two pieces, so
the gap and the frame difference are both visible.

## The fix, and the one the diagnosis proposed that we are NOT taking

The diagnosis suggested sampling the decoration along the path so it follows the curve. **Do not do
that.** A decoration is a rigid solid; an arrowhead bent along an arc looks worse than a detached
one, and the whole point of an arrow tip is that it points in one direction.

The decoration stays rigid in the endpoint frame. **The shaft gives way**: its boundary ring on a
decorated end becomes the decoration's back ring - same positions, same frame - so the last stretch
of shaft absorbs the bend and the two pieces share a boundary.

The contract is written out in the comment above `BuildStroke` in
`src/Renderer/Path/PathStrokeMesher.hpp`. Implement that comment.

Second, separate, and latent: a decorated end whose decoration closes its back still gets a shaft
cap, so a `Round` cap and the decoration's back closure occupy the same space
(`AppendTubePiece`, `PathStrokeMesher.cpp:155-185`). Suppress the cap on that end. This is NOT the
defect the user saw - `PathStrokeStyle::cap` defaults to `Butt`, the dev presets do not change it
(`ScenePathDevMenu.cpp:16-32`) and the Properties panel has no cap control - but it is the same
handoff and it is cheap to close while you are here. An unfilled decoration does not close its
back, so that end keeps its cap.

## Files to create or change

- `src/Renderer/Path/PathStrokeMesher.cpp` - `BuildStroke`, `AppendDecoration`, `AppendTubePiece`
- `src/Renderer/Path/PathDecoration.cpp` - only if `TrimmedRange` needs to report the back plane;
  prefer not to change it

## Files that must NOT be touched

- `src/Renderer/Path/PathStrokeMesher.hpp` - written, it is the contract
- `src/Renderer/Path/PathDecoration.hpp`, `src/Renderer/Path/PathStyle.hpp` - S11i owns those and
  is a separate commit
- `src/Renderer/Path/PathTessellator.cpp`, `PathFrames.cpp` - the frames are correct; this is a
  consumer bug, and changing the tessellator to paper over it would move the defect, not fix it
- `src/Renderer/OpenGl/` - no shader change. The CameraFacing shader's expansion rule is right; it
  is the centreline vertex it is handed that is in the wrong place.
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. For a curved path (`Arc` or `Cubic`) with an end decoration, in the `Round` profile: the shaft's
   boundary ring and the decoration's back ring have identical vertex positions. Not "close" -
   identical, because they are the same ring.
2. The same holds for `Flat`: the ribbon's boundary vertex pair sits at the decoration's back
   centreline position, with the decoration's tangent and normal.
3. The same holds for `CameraFacing`: the boundary pair carries the decoration's back centreline
   position, tangent and half-width, so the shader expands both sides of the join about one point.
4. A straight `Line` path's output is unchanged by this task, because endpoint frame and trim frame
   already coincided there. This is the regression guard.
5. A decoration at the start behaves the same as one at the end.
6. A path with decorations at both ends keeps a well-formed shaft between them.
7. With `cap == Round` and a back-closing end decoration, no hemisphere cap is emitted on that end.
8. With `cap == Round` and NO decoration on that end, the cap is emitted exactly as before.
9. With `cap == Round` and an unfilled (non-back-closing) decoration, the cap on that end is still
   emitted - the decoration does not close the tube, so something has to.
10. `StrokeGeometry::shaft`, `startDecoration` and `endDecoration` still describe non-overlapping
    index ranges, and every index is in bounds for the populated vertex array.
11. Every invariant already in the `BuildStroke` contract still holds - in particular no NaN or Inf
    in any vertex field at a 0, 90 or 180 degree bend, and empty geometry rather than a crash for a
    zero-length path.
12. `DecorationsExceedPathLength` still behaves as documented: decorations built, shaft not.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` is the exception-free zone: no `throw`.
- `.cpp` files stay under ~500 lines. `PathStrokeMesher.cpp` is 320. If this and S11i together push
  it past ~450, split the decoration meshing into its own file - that split is expected, not a
  surprise.
- `BuildStroke` must stay camera-independent: the same path and style produce byte-identical output
  from any viewpoint. That is why CameraFacing defers expansion to the shader, and it is what makes
  the mesh testable without GL at all.
- S11g and S11i may land before or after this. Do not touch their files.
- Do not build and do not run tests - the sandbox cannot, and the verifying session does it.
- Do not change any test file or any test expectation.

## If the diagnosis turns out to be wrong

The fix above is wrong if the reported screenshots were of a straight `Line` path, where the two
frames already agree. If, while implementing, you find evidence that a straight path also shows the
mismatch, stop and report it rather than widening the change - that would be a different bug and it
would need its own diagnosis.
