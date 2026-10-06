# Task 70: C_2 ring about the bond axis

## Goal

Selecting two atoms and adding a curved arrow currently draws a shallow arc lying in the plane
between them, turning about the defect z. It should instead draw a C_2 ring about the bond those
two atoms share: an arc in the plane perpendicular to the bond, centred on its midpoint, clear of
both spheres, and following the atoms when either one moves. Three or more selected atoms keep
today's cycle about the defect z, unchanged.

## Files to create or change

- `src/Presentation/Panels/ScenePathCurvedArrow.cpp` — the work. `TwoEndAxis` and
  `kCurvedArrowFlatness` in its anonymous namespace are what the bond mode replaces for two ends.
- `src/Renderer/Path/PathBindingResolver.cpp` (and its `.hpp` only if a declaration is missing) —
  resolve `PathTransformBinding::BondFrame` into the path's transform.
- `src/IO/SceneObjectsYaml.cpp`, `src/IO/SceneObjectsIO.hpp` — persist `transformBinding`.
  A path without one must load exactly as it does today; do not bump the format version.
- `src/Presentation/Panels/ViewportAddMenu.cpp:114` — the single call site, only if it needs to
  pass parameters.

## Files that must NOT be touched

- `src/Renderer/Path/CurvedArrowParameters.hpp` — the contract. Signatures are fixed.
- `src/Presentation/Panels/ScenePathCurvedArrow.hpp` — the contract. Signatures are fixed.
- `src/Renderer/Path/PathTypes.hpp` — `PathTransformBinding` and `ScenePath::transformBinding` are
  already written. Do not add fields or rename them.
- `tests/Presentation/Panels/SceneCurvedArrowTests.cpp` — the contract. Do not change a single
  expectation. If one looks wrong, stop and say so in your report instead of editing it.
- Anything under `src/Domain/` — this task does not touch the domain.
- `src/Presentation/Operators/`, `src/Presentation/Panels/OperatorRedoPanel.*` — later tasks.

## Acceptance criteria

1. `SceneCurvedArrowTests.BondModeArcLiesInThePlanePerpendicularToTheBond` passes.
2. `SceneCurvedArrowTests.BondModeRadiusClearsTheLargerSphere` passes.
3. `SceneCurvedArrowTests.BondModeSweepIsClampedBelowAFullTurn` passes.
4. `SceneCurvedArrowTests.BondModeZeroAtomRadiusStillProducesAVisibleArc` passes.
5. `SceneCurvedArrowTests.BondModeRejectsACoincidentPair` passes.
6. `SceneCurvedArrowTests.BondModeRingFollowsTheAtomsWhenOneMoves` passes.
7. `SceneCurvedArrowTests.AutoPicksTheBondForTwoEndsAndTheDefectZAbove` passes.
8. Every other test in `SceneCurvedArrowTests` still passes, unedited — including the five that now
   pass `CurvedArrowAxisMode::DefectZ` explicitly.
9. The full Release suite is green apart from the two known
   `DS_PYTHON_CAPI_AVAILABLE=0` skips.
10. A project saved with a bond-bound ring reloads with the ring in the same place, and a project
    saved before this change loads unchanged.

## Constraints

- Resolved radius: `AtomRelative` gives `max(radiusA, radiusB) * radiusFactor`, falling back to
  `0.35 * bondLength` when that product is below `1.0e-4`; `BondFraction` gives
  `bondLength * radiusFactor`.
- `sweepDegrees` is clamped to `[1, 350]`.
- `rotationDegrees` becomes `BondFrame::rollRadians`, so the stored binding remembers where the
  user put the arc.
- Reject a coincident pair with a `StructuredError` through the existing `ArrowError` helper, code
  `curved_arrow.degenerate_bond`. Never emit a non-finite coordinate.
- `PathBinding::BondMidpoint` already exists and already persists. Look at how it is read and
  written before inventing anything for `BondFrame` — follow that pattern rather than a new one.
- Layer boundaries from `AGENTS.md` are hard. `Domain` stays out of this.
- `.cpp` files stay under ~500 lines. `ScenePathCurvedArrow.cpp` is at 191; if the bond mode pushes
  it past the limit, split the arc construction into its own file under `src/Renderer/Path/`.
- Regenerate projects with `scripts/Windows/GenerateProjects.bat` after adding any new file.
- Build and test Release only: `scripts/Windows/Build.bat --config Release`.
