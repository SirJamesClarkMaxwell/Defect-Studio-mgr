# Task 41 S5: stroke mesh, dashes, gradient, decorations (pure CPU)

## Goal
A `ScenePath` plus its style becomes drawable geometry, entirely on the CPU and entirely
camera-independent: a swept tube for the Round profile, a centreline ribbon for Flat/CameraFacing,
world-space dashes whose phase runs continuously across segment boundaries, a per-vertex gradient over
normalised arc length, and eight endpoint decorations described once as an axial contour and consumed by
both profiles. Nothing is drawn yet - S7 uploads this. The legacy arrow dash logic stops being a second
implementation and starts calling the new one.

## Files to create
- `src/Renderer/Path/PathDash.cpp`
- `src/Renderer/Path/PathDecoration.cpp`
- `src/Renderer/Path/PathStrokeMesher.cpp`
- `tests/Renderer/Path/PathDashTests.cpp`
- `tests/Renderer/Path/PathDecorationTests.cpp`
- `tests/Renderer/Path/PathStrokeMesherTests.cpp`

## Files you may change
- `src/Renderer/Path/PathTopology.cpp` - `ReversePath` only: it must now also swap
  `style.startDecoration` with `style.endDecoration`, mirror every gradient stop position to
  `1 - position` and reverse the stop order, and adjust `style.dash.phase` so the dash pattern lands on
  the same world positions after reversal. Nothing else in that file changes.
- `src/Renderer/Scene/SceneObjectAppearance.cpp` - `BuildSceneArrowShaftSegments` only: its body becomes
  a call to `BuildDashIntervals`, converting the legacy `(shaftLength, dashed, dashLength, gapLength)`
  arguments into a `PathDashStyle` and the returned `DashInterval`s into `SceneArrowShaftSegment`s. The
  signature, the header and `SceneObjectAppearanceTests` stay exactly as they are - the wrapper exists so
  the legacy arrow renderer keeps working unchanged until S16.
- `tests/Renderer/Path/PathTopologyTests.cpp` - add reversal-of-style cases only; do not alter existing
  cases.

## Files that must NOT be touched
- `src/Renderer/Path/PathStyle.hpp`, `PathDash.hpp`, `PathDecoration.hpp`, `PathStrokeMesher.hpp` - the
  contract. If a signature is wrong, STOP and say which and why; do not edit it.
- `src/Renderer/Path/PathTypes.hpp`, `PathEvaluator.{hpp,cpp}`, `PathFrames.*`, `PathLod.*`,
  `PathTessellator.*`, `PathBindingResolver.*`, `PathHandleRules.*`, `PathTopology.hpp` - already done.
  The style field and the three new diagnostic codes (`InvalidStrokeStyle`, `InvalidGradient`,
  `DecorationsExceedPathLength`) are already added.
- `src/Renderer/Scene/SceneObjectAppearance.hpp`, `src/Renderer/Scene/SceneArrowGeometry.*`,
  `tests/Renderer/Scene/*` - the legacy arrow path must keep passing its existing tests untouched.
- `premake5.lua`, anything under `src/Presentation/`, `src/App/`, `src/Domain/`, `src/IO/`,
  `src/Renderer/OpenGl/`.

## Acceptance criteria
1. `BuildDashIntervals` returns ordered, disjoint intervals clipped to `[rangeStart, rangeEnd]`. A
   disabled pattern, or a non-positive / non-finite dash or gap length, returns the single interval
   `[rangeStart, rangeEnd]`. An empty or inverted range returns nothing.
2. Dash phase is anchored at arc length 0, not at `rangeStart`: for a fixed pattern, the interval
   boundaries that fall inside a sub-range are at the same world positions whether the range is
   `[0, L]` or `[a, L]`. A test must assert exactly this (it is what makes a trimmed shaft keep its
   dashes aligned, and what makes phase continuous across a Line to Arc boundary on a mixed path).
3. Negative and larger-than-period phases wrap to the same pattern as their in-period equivalent.
4. A period so small relative to the range that the pattern is not representable (guard it - do not
   loop millions of times) returns the single solid run instead.
5. `DashCoverage` over a full period pattern equals the expected on-fraction of the range, within 1e-9
   for an exact multiple of the period.
6. `BuildSceneArrowShaftSegments` behaves exactly as before for every existing
   `SceneObjectAppearanceTests` case, and is implemented by calling `BuildDashIntervals`. Match the
   legacy thresholds: `shaftLength <= 0.0001f` returns empty; a non-dashed or sub-threshold dash/gap
   returns one span `[0, shaftLength]`.
7. `BuildDecorationContour` returns an empty contour with `trim == 0` for `None`, a non-positive or
   non-finite scale, and a non-positive stroke width. For every other kind it returns at least three
   points, ordered by non-decreasing `s` starting at `s == 0`, all values finite, and `halfWidth >= 0`.
8. All eight kinds are distinguishable: Arrow closes its back, Stealth does not, OpenArrow is the only
   `filled == false`, Bar is short and wide, Circle/Square/Diamond differ in profile shape. Assert the
   properties, not exact coordinates - the proportions are yours to pick, but keep them close to the
   legacy `GetArrowTipParameters` table (`Plain 1.0/1.0`, `Barbed 1.15/1.10`, `Open 1.0/1.0`,
   `Bar 0.08/1.25`, `Circle 0.75/0.75`) so a converted arrow looks like itself.
9. `TrimmedRange(totalLength, style)`: `start` equals the start decoration's `trim`, `end` equals
   `totalLength - endDecoration.trim`, and when the two meet or overlap the range is empty, never
   inverted. A test asserts trim distance equals decoration insertion length.
10. `SampleStrokeColor` returns the flat style colour and alpha for a disabled or empty gradient;
    otherwise it clamps at both ends (no extrapolation), interpolates RGB and alpha linearly, and hits
    each stop's exact colour at that stop's position.
11. `BuildStroke` with an invalid style (non-positive or non-finite width, `radialSegments < 3`) emits
    `InvalidStrokeStyle` and returns no geometry. A malformed gradient (non-finite, out-of-`[0,1]` or
    decreasing stop positions) emits `InvalidGradient` and returns no geometry.
12. Round profile: the shaft is an indexed triangle list, every vertex normal is finite and unit-length
    within 1e-6, and there are `radialSegments` vertices per ring. Tested at bends of 0, 90 and 180
    degrees (a straight line, a right-angle two-segment path, a path that doubles back) - the 180 degree
    case is the one that produces a parallel cross product in a naive mesher, and it must not.
13. Flat / CameraFacing profile: `ribbonVertices` is populated and `tubeVertices` is empty (and the
    reverse for Round), `side` is exactly -1 or +1 on every vertex, and each centreline sample
    contributes exactly two vertices.
14. Joins and caps: no bend leaves a hole; Butt, Square and Round caps each produce the expected
    vertex/index count relation, and Butt adds no cap geometry.
    **Deviation, taken deliberately:** `style.join` has no effect on the Round profile and is not
    consumed at this stage. A ring per sample stitched to its neighbour is already closed at a bend,
    whatever the join, so Bevel and Round would produce identical tubes; the join only becomes
    load-bearing in S7, where the ribbon profiles are expanded in the shader and it decides what fills
    the wedge. Building join geometry now would mean guessing at a shader that does not exist. The
    header states this, and a test asserts the two joins produce identical output so it cannot start
    differing silently.
15. Dashed strokes: the shaft consists of one piece of geometry per dash interval, `dashedLength` equals
    `DashCoverage` of those intervals within 1e-9, and `dashCoord` on every vertex equals that vertex's
    world arc length.
16. `arcT` is monotonically non-decreasing along the shaft and spans the trimmed range, and every
    per-vertex colour matches `SampleStrokeColor` at that vertex's `arcT`.
17. Decorations: every kind produces non-empty, non-degenerate geometry (non-zero triangle area) in BOTH
    profiles, and `startDecoration` / `endDecoration` ranges index into the mesh correctly (first index
    plus count within bounds).
18. `DecorationsExceedPathLength` is emitted when the two trims meet or overlap; the decorations are
    still built and the shaft is not.
19. No NaN or Inf in any vertex field for any of: a zero-length path, a single-sample path, a path with
    coincident consecutive samples, a 180 degree doubling-back path, a degenerate style.
20. `ReversePath` on a styled path swaps the decorations, mirrors the gradient (stop `p` becomes `1 - p`,
    order reversed), and adjusts the dash phase so the dashes land on the same world positions; a test
    asserts all three, and the existing `ReversePath` tests still pass.
21. Full Release test suite green except the two permanent skips
    (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
    `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`).

## Constraints
- Layer: `src/Renderer/Path/` and `tests/` only, plus the two named surgical edits. `Renderer` is the
  documented exception-free zone - no `throw`, failures are diagnostics on the returned struct.
- No `std::thread`. No OpenGL, no camera, no window state: this stage is pure CPU geometry and must stay
  unit-testable without a GL context.
- Evaluate in `double`, store in `float` (plan v2 C9). Tolerances in tests follow S3's note: 1e-9 for
  pure-double relations, 1e-6 where a `float` field round-trips.
- `.cpp` files stay under ~500 lines. `PathStrokeMesher.cpp` is the one at risk - if it grows past that,
  say so rather than cramming; do not silently add a fourth file.
- Do not add a second dash implementation, a second decoration table, or a second gradient sampler.
  `PathDash` is the only interval generator in the renderer after this task.
- Style: tabs, `#include "Core/dspch.hpp"` first, anonymous namespace for helpers, `[[nodiscard]]`.
- Tests: GoogleTest, namespace `DefectStudio::Tests`, matching `tests/Renderer/Path/PathEvaluatorTests.cpp`.
