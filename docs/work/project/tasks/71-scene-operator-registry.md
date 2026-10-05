# Task 71: Scene operator registry

## Goal

Give the application a place to register named, re-runnable scene operations that carry a data
parameter schema, and make the C_n curved arrow the first one. After this task a caller can look up
`"scene.curved_arrow"`, read its schema and defaults, and run it with a value map - without knowing
anything about `CurvedArrowParameters`. The redo panel in the next task renders widgets purely from
that schema, so nothing about the arrow leaks into the panel.

## Files to create or change

- Create: `src/Presentation/Operators/SceneOperatorRegistry.cpp` - the registry. A flat
  `std::unordered_map<std::string, SceneOperator>`; `Register` rejects a duplicate id with a
  `StructuredError` and keeps the first registration; `ListIds` returns a sorted vector.
- Create: `src/Presentation/Operators/CurvedArrowOperator.cpp` - builds the schema and defaults,
  converts a `SceneOperatorValues` into a `CurvedArrowParameters`, and calls
  `AddCurvedArrowThroughSelectedAtoms`. Keep that conversion in one function; the panel must never
  construct a `CurvedArrowParameters` itself.

## Files that must NOT be touched

- `src/Presentation/Operators/SceneOperator.hpp` - the contract. Do not add or rename a field.
- `src/Presentation/Operators/SceneOperatorRegistry.hpp` - the contract. Signatures are fixed.
- `tests/Presentation/Operators/SceneOperatorRegistryTests.cpp` - the contract. Do not change a
  single expectation. If one looks wrong, stop and say so in your report instead of editing it.
- `src/Renderer/Path/CurvedArrowParameters.hpp` - fixed.
- `src/Presentation/Panels/ScenePathCurvedArrow.{hpp,cpp}` - task 70 shipped these. The operator
  calls the existing entry point; it does not reach into the arrow's geometry.
- `src/Presentation/Panels/ViewportAddMenu.cpp` - the Add menu switches to the operator in task 72,
  not here. This task only registers; nothing calls it from the UI yet.
- `src/Presentation/Panels/OperatorRedoPanel.*` - a later task.
- Anything under `src/Domain/`.

## Acceptance criteria

Every test in `tests/Presentation/Operators/SceneOperatorRegistryTests.cpp` passes, unedited:

1. `RegisterThenFindReturnsTheOperator`
2. `RegisteringADuplicateIdIsRejected`
3. `ListIdsIsSortedAndComplete`
4. `CurvedArrowOperatorExposesRadiusSweepAndRotation`
5. `CurvedArrowDefaultsMatchTheParameterStruct`
6. `CurvedArrowExecuteBuildsTheRingForTwoSelectedAtoms`
7. `CurvedArrowExecuteHonoursAChangedSweep`
8. `CurvedArrowExecuteFailsWithoutASelection`
9. The full Release suite shows no new failures. Known and expected, not regressions: two
   `DS_PYTHON_CAPI_AVAILABLE=0` skips, and 17 failing `PointGroupAnalysisBridgeTests` caused by a
   `groupy` library API change outside this repo.

## Constraints

- Schema keys, exactly these eight: `axisMode`, `radiusRule`, `radiusFactor`, `sweepDegrees`,
  `rotationDegrees`, `decoration`, `color`, `strokeWidth`. Labels are what a user reads in the
  panel - Polish, matching the rest of the UI ("Promień", "Rozpiętość", ...). Ids and keys stay
  English.
- Enum parameters store a `int` index into `enumLabels`. `axisMode` has three labels in
  `CurvedArrowAxisMode` order; `radiusRule` has two in `CurvedArrowRadiusRule` order. `decoration`
  is also an enum - give it a sensible label list in `PathDecorationKind` order and store the index
  the same way.
- `sweepDegrees` range must sit inside `[1, 350]`, because the arrow clamps to that. A slider that
  can ask for a value the operator silently discards is a bug.
- Reading a value: a missing key or a wrong alternative falls back to the `CurvedArrowParameters`
  default for that field. The panel may hand over a partial map; that must not crash or produce
  garbage geometry.
- Layer boundaries from `AGENTS.md` are hard. `Presentation/Operators` may include
  `Presentation/Panels` and `Renderer`; nothing in `Domain` is touched.
- `.cpp` files stay under ~500 lines.
- Regenerate projects with `scripts/Windows/GenerateProjects.bat` after adding any new file. It has
  already been run once for the two headers and the test file; run it again if you add a file.
- Do not build. The build environment here denies file access and a build you start will fail in a
  way that tells you nothing. Report what you changed; the build and the test run happen outside
  your sandbox.
