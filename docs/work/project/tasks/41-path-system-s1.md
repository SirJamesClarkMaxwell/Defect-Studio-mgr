# Task 41 S1: path model, evaluator, binding resolver (pure)

Design contract: `docs/work/project/plans/2026-09-20-path-system-redesign-v2.md` (wins on any design
question). Stage plan: `docs/work/project/plans/2026-09-20-path-system-implementation.md`, section
"S1 - model, evaluator, binding resolver (pure)" - read that section before writing code.

## Goal

The first stage of the ScenePath system: a pure geometry core with no renderer, UI or GL dependency.
After this task the repo can describe a path (nodes + Line/Cubic/Arc segments), validate it, evaluate
position/tangent/arc-length anywhere on it, and turn authored node positions into resolved positions
through atom/bond/object bindings. Nothing user-visible changes; nothing outside `src/Renderer/Path`
and `tests/Renderer/Path` is touched.

## Files to create or change

Create (implementation):
- `src/Renderer/Path/PathEvaluator.cpp`
- `src/Renderer/Path/PathBindingResolver.cpp`

Create (tests):
- `tests/Renderer/Path/PathModelTests.cpp`
- `tests/Renderer/Path/PathEvaluatorTests.cpp`
- `tests/Renderer/Path/PathBindingResolverTests.cpp`

Already written, THE CONTRACT, do not change:
- `src/Renderer/Path/PathTypes.hpp`
- `src/Renderer/Path/PathEvaluator.hpp`
- `src/Renderer/Path/PathBindingResolver.hpp`

## Files that must NOT be touched

- Every existing `.cpp`/`.hpp` outside `src/Renderer/Path/` - in particular `SceneArrowGeometry.*`,
  `RendererWindowState.hpp`, `SceneObject.hpp`, `SceneSystem.cpp`, anything under `src/Presentation/`,
  `src/App/`, `src/IO/`, `src/Domain/`.
- The three headers above. If a signature looks wrong, STOP and say so in the final report instead of
  changing it.
- `premake5.lua`, `.vcxproj` files (regeneration is run by the script, not edited by hand).

## Acceptance criteria

Each is one or more GoogleTest `TEST` cases in the files above. Names are yours; coverage is not.

1. Line segment: position at `t`, unit tangent, `SegmentLength` == chord length.
2. Cubic: endpoints exact at `t=0`/`t=1`; tangent at the ends parallel to the handle direction;
   length monotone in handle length; a symmetric S-curve's length matches a known value within `1e-9`
   (bounded-error Gauss-Legendre with subdivision - NOT a sum over samples).
3. Arc: a 120 degree sweep over chord `|AB|` gives `r == |AB| / sqrt(3)`; quarter, half and negative
   sweeps; the endpoint is hit at `t=1` within `1e-12`; `SegmentLength == r * |theta|`; flipping the
   sweep sign flips the direction of travel.
4. Arc rejection: zero chord, normal parallel to the chord, `|theta|` out of range, non-finite input -
   each returns the named `PathDiagnosticCode` / a `StructuredError`, never a NaN value.
5. `max_digits10` round-trip: `signedSweepRadians` for 120 degrees survives float -> text -> float
   bit-identically.
6. Mixed Line+Cubic+Arc path: `CumulativeLengths` has size `segments + 1`, is monotone, and its back
   equals the sum of the segment lengths; `SegmentParamAtLength` inverts `SegmentLength` within
   tolerance on each segment type.
7. `ValidatePath`: N nodes / N-1 segments mismatch, duplicate element ids, NaN in authored data -
   each produces its diagnostic code; a sound path produces an empty vector.
8. Resolver - `Free` returns the authored position; `CopyPosition` applies offset; `BondMidpoint` is
   the midpoint of the two atoms plus offset; `ObjectOrigin` uses the injected origin.
9. Resolver - endpoint `CopyPosition` with `buffer != 0` offsets toward the sole neighbour's
   UNBUFFERED position and applies the existing non-inversion clamp (never moves past the neighbour);
   an interior node with `buffer != 0` keeps the unbuffered position and reports `InteriorNodeBuffer`.
10. Resolver - a missing atom (context returns `nullopt`) falls back to the authored position and
    reports `BrokenBinding`; `ObjectOrigin` naming a path reports `ObjectOriginTargetsPath`.
11. Resolver purity: the input `ScenePath` is unchanged after `ResolveNodePositions` (compare every
    field, including `nextElementId`).
12. `AllocateElementId` hands out monotonically increasing, never-reused ids.
13. The existing `SceneArrowGeometryTests` still pass, untouched.

## Constraints

- Layer: `Renderer` only. No include of `Presentation`, `App`, `IO`, ImGui, GLFW or any GL header.
  No `Domain` include either - the resolver reaches the scene only through `BindingContext`.
- No exceptions on any evaluation path. Errors are `Result<T>` + `StructuredError`
  (`Core/Diagnostics/StructuredError.hpp`), or a `PathDiagnostic` where the header says so.
- Storage is `float`, evaluation is `double` (plan v2 C9). Convert only at the boundary.
- `.cpp` files stay under ~500 lines - split into a second `.cpp` in the same folder if needed
  (and say so in the report, because the project must then be regenerated).
- Tabs for indentation, `DefectStudio` namespace, brace style as in `src/Renderer/Scene/*.cpp`.
- Every `.cpp` starts with `#include "Core/dspch.hpp"` (precompiled header, mandatory).
- Tests live in `namespace DefectStudio::Tests`, include `<gtest/gtest.h>`, and follow
  `tests/Renderer/SceneTransformTests.cpp` for style.
- New files require `scripts/Windows/GenerateProjects.bat` (premake globs at generation time).
- Do not build in this sandbox and do not wait for an approval step - the dispatching session builds
  and verifies.
