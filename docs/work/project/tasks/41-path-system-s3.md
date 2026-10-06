# Task 41 S3: path tessellation, frames and LOD buckets

## Goal
A `ScenePath` can be turned into a polyline of samples whose chord-height error is bounded by a
requested world tolerance, each sample carrying a parallel-transported orthonormal frame, arc length
and normalized parameter. Screen density is quantised into hysteretic power-of-two LOD buckets so a
zoom sweep re-tessellates a few times instead of every frame. Still pure C++: no GL, no UI, no
renderer state - S5 (stroke mesh) and S7 (GL renderer) are the consumers.

## Files to create
- `src/Renderer/Path/PathFrames.cpp`
- `src/Renderer/Path/PathLod.cpp`
- `src/Renderer/Path/PathTessellator.cpp`
- `tests/Renderer/Path/PathFramesTests.cpp`
- `tests/Renderer/Path/PathLodTests.cpp`
- `tests/Renderer/Path/PathTessellatorTests.cpp`

## Files you may change
- `src/Renderer/Path/PathEvaluator.cpp` - ONLY to add the three new names to `PathDiagnosticCodeName`
  (`TessellationDepthLimit`, `TessellationSampleLimit`, `InvalidTessellationSettings`). Nothing else in
  that file.

## Files that must NOT be touched
- `src/Renderer/Path/PathFrames.hpp`, `PathLod.hpp`, `PathTessellator.hpp`, `PathTypes.hpp`,
  `PathEvaluator.hpp`, `PathBindingResolver.hpp`, `PathTopology.hpp`, `PathHandleRules.hpp` - these are
  the contract. If a signature is wrong, STOP and say which one and why; do not edit it.
- Every existing `.cpp` except the `PathDiagnosticCodeName` addition above.
- Everything outside `src/Renderer/Path/` and `tests/Renderer/Path/`.
- `premake5.lua` (premake globs sources; regeneration is enough).

## Acceptance criteria
1. `Tessellate` on a two-node Line path returns exactly 2 samples, `arcLength` 0 and the chord length,
   `normalizedT` 0 and 1, `totalLength` == chord length.
2. Cubic: maximum distance from every returned sample's neighbourhood midpoint to the analytic curve
   (sample the true cubic densely, e.g. 4096 points, and measure each dense point's distance to the
   returned polyline) is <= `worldTolerance`. Test at tolerances 1e-1, 1e-2, 1e-3.
3. Sample count is monotone non-decreasing as `worldTolerance` shrinks, for both Cubic and Arc.
4. Arc: same polyline-error bound as criterion 2, tested for a +120 degree and a -120 degree sweep.
5. A mixed Line/Cubic/Arc path (>= 3 segments) produces samples in strictly non-decreasing `arcLength`,
   with exactly one sample per interior node (no duplicated positions at segment boundaries), and
   `samples.back().arcLength == totalLength` to 1e-9.
6. `totalLength` agrees with `CumulativeLengths(path, resolved).back()` within `worldTolerance`.
7. No sample field is NaN or Inf for degenerate fixtures: coincident nodes, a zero-length line, an arc
   whose sweep is out of range, a path failing `ValidatePath`. The degenerate-path cases return no
   samples plus the matching diagnostic; nothing crashes and nothing asserts.
8. `maxDepth = 1` on a tight tolerance emits a `TessellationDepthLimit` diagnostic; `maxSamplesPerSegment
   = 4` on a tight tolerance emits `TessellationSampleLimit`. Both still return the samples reached.
9. Non-finite or non-positive `worldTolerance`, `maxDepth < 1` or `maxSamplesPerSegment < 2` emits
   `InvalidTessellationSettings` and returns no samples.
10. Frames: every sample's `tangent`, `normal`, `binormal` are unit to 1e-9, mutually orthogonal to 1e-9,
    and right-handed (`cross(tangent, normal)` == `binormal` to 1e-9).
11. No frame flip: over an arc of sweep `2*pi - 1e-3` and over a planar S-curve (two cubics), the dot
    product of consecutive samples' normals is > 0 for every consecutive pair.
12. `FrameSeed::Mode::FixedNormal` with a normal perpendicular to the start tangent reproduces that
    normal in the first sample to 1e-9; with a normal parallel to the start tangent it falls back to a
    valid frame instead of producing NaN.
13. `SeedFrame` with a zero / NaN tangent returns a finite orthonormal frame.
14. `TransportFrame` across a tangent that passes exactly through world up (+Z) produces no
    discontinuity: consecutive normals stay within 1e-6 of the minimal-rotation result.
15. `QuantiseLod(d, kNoLodBucket)` == `clamp(floor(log2(d)), kMinLodBucket, kMaxLodBucket)`; density <= 0
    or non-finite yields `kMinLodBucket`.
16. Hysteresis: sweeping `pixelsPerWorldUnit` from 2^3/2 up to 2^5 and back down in 200 steps, feeding
    each result back as `previousBucket`, changes bucket at most once per boundary crossing per
    direction (i.e. no oscillation - count the transitions and assert the expected small number).
17. `ToleranceForLod(bucket, budget)` == `budget / 2^bucket`, always finite and > 0 for a finite budget
    > 0.
18. Full Release test suite green except the two permanent skips
    (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
    `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`).

## Constraints
- Layer: `Renderer` only. No include of `Presentation/`, `App/`, `Domain/`, `IO/` from these files.
- No exceptions anywhere (`Renderer` is the documented exception-free zone). Errors are diagnostics in
  `EvaluatedPath::diagnostics`, or `Result<T>` where the header says so.
- No `std::thread`, no globals, no I/O, no logging.
- `float` storage, `double` evaluation (plan v2 C9). All tessellation math in `double`.
- `.cpp` files stay under ~500 lines. Split with an anonymous namespace rather than a new header.
- GLM constructors in this build are explicit: write `glm::dvec3(0.0)`, never `{0.0}`.
- Reuse `EvaluateSegment`, `SegmentLength`, `CumulativeLengths` and `ValidatePath` from
  `PathEvaluator.hpp`. Do not re-derive curve math that already exists there.
- Follow the existing style of `PathEvaluator.cpp` / `PathTopology.cpp`: tabs, `#include "Core/dspch.hpp"`
  first, anonymous namespace for helpers and constants, `[[nodiscard]]`.
- Tests: GoogleTest, namespace `DefectStudio::Tests`, same shape as `tests/Renderer/Path/PathEvaluatorTests.cpp`.

## Tolerance note
Tolerances above are deliberately 1e-9 / 1e-6 and not tighter: node positions and arc sweeps are stored
as `float` (plan v2 C9), so any quantity derived from authored data carries ~1e-7 relative float error
into the double evaluation. Do not tighten these; do not loosen them either without saying why.
