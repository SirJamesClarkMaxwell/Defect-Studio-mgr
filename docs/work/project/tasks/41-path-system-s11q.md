# Task 41 S11q: a gradient is sampled where its stops are

## Goal

A three-stop gradient - red, green, blue - renders as red fading to blue with the green never
appearing. The manual round handed over the deciding detail: it works on a dashed line and not on
a solid one.

## The cause

The stroke's colour is evaluated per vertex, `SampleStrokeColor(style, sample.normalizedT)`
(`PathStrokeMesher.cpp:177`), and the tessellator subdivides on geometry alone - chord deviation
from the true curve (`PathTessellator.hpp:16`). A straight `Line` is exact at two samples, so it
gets two: one at each end. Red at 0, blue at 1, and the middle stop is never sampled.

Dashes cut the shaft into runs, each run brings its own vertices, and the gradient appears. That is
the entire difference between the two cases.

## The contract, already written

The S11q bullet in the `BuildStroke` contract comment in
`src/Renderer/Path/PathStrokeMesher.hpp`. In short: when the gradient is enabled, the shaft is
additionally subdivided at every stop's arc position; two stops at the same position give two
coincident samples so the colour steps instead of blending; a disabled or empty gradient adds
nothing.

The extra samples lie on the curve, so this changes colour resolution and not shape.

## Files to create or change

- `src/Renderer/Path/PathStrokeMesher.cpp` - the shaft's sample list. `AtLength` already samples
  the path at an arc length, and the dash walk already splits the shaft into pieces, so the
  machinery exists. This is another source of split positions, not a new mechanism.

## Files that must NOT be touched

- `src/Renderer/Path/PathStrokeMesher.hpp` - the contract
- `src/Renderer/Path/PathTessellator.{hpp,cpp}` - the tessellator subdivides for geometry and must
  not learn about style. Pushing the gradient into it puts a rendering concern in the evaluator.
- `src/Renderer/Path/PathDecorationMesher.cpp` - decorations sit at the ends and sample the
  endpoint; they are unaffected
- `src/Renderer/OpenGl/` - no shader change. The colour stays a vertex attribute.
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. A straight `Line` with a three-stop gradient has a vertex at the middle stop's arc position, and
   the colour there equals that stop's colour.
2. The same holds for an arbitrary number of interior stops.
3. A gradient whose stops sit only at 0 and 1 adds no samples beyond what geometry already
   required.
4. A disabled gradient produces byte-identical geometry to before this task. So does an enabled one
   with no stops.
5. Two stops at the same position produce two samples at that arc length carrying the two different
   colours, so the colour steps.
6. The extra samples lie on the path: their positions equal `AtLength` at their arc length, within
   tolerance. Subdividing must not alter the shape.
7. A dashed stroke still works, and a stop falling inside a gap does not resurrect the gap.
8. The shaft's index range stays contiguous and disjoint from both decorations', and every index is
   in bounds.
9. Stops outside the shaft's trimmed range - under a decoration - do not produce samples outside
   that range.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` stays exception-free.
- `.cpp` files stay under ~500 lines.
- `BuildStroke` stays camera-independent.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
