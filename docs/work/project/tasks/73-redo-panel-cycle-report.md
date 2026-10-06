# Task 73 report: redo controls for curved-arrow cycles

Implemented on `task/70-operator-redo-panel`. Changes remain in the working tree; no commit.

## Files changed

- `src/Renderer/Path/CurvedArrowParameters.hpp`: added `curvature`, default `0.5`.
- `src/Presentation/Panels/ScenePathCurvedArrow.hpp` and `.cpp`: shared selection-mode resolver; non-bond arcs use curvature instead of the fixed flatness.
- `src/Presentation/Operators/SceneOperator.hpp`: optional `isParameterRelevant` callback.
- `src/Presentation/Operators/CurvedArrowOperator.cpp`: curvature schema/default/value conversion and mode-specific relevance.
- `src/Presentation/Panels/OperatorRedoPanel.hpp` and `.cpp`: private hidden-key cache, refreshed with the original input selection before each execution; drawing skips hidden keys.
- `tests/Presentation/Panels/OperatorRedoPanelTests.cpp`: replaced the diagnostic probe with regression assertions and added mode-switch and empty-callback cases.
- `tests/Presentation/Operators/SceneOperatorRegistryTests.cpp`: added curvature schema/sweep/relevance and valid-index/vacancy selection cases.
- `tests/Presentation/Panels/SceneCurvedArrowTests.cpp`: added curvature clamping/default coverage for cycles and non-bond pairs.
- This report.

## Decisions

- Curvature multiplies the signed angle between each pair of ends. Its default preserves the existing shallow C_3 arc; finite values clamp to `[0.05, 1.5]`, and nonfinite values use `0.5`. Existing path validation still rejects invalid geometry before any batch is committed.
- The operator exposes `Wygięcie łuku` as a Float with that same range/default.
- `ResolveCurvedArrowSelectionMode` counts in-range selected atom indices. Three or more select cycle mode; otherwise explicit Bond or Auto with two valid atoms selects bond mode. Vacancy pairs keep the existing non-bond Auto behavior. Explicit Bond still requires two atom ends during creation.
- The curved-arrow callback exposes exactly these sets:
  - Cycle: curvature, decoration, color, strokeWidth.
  - Bond pair: axisMode, radiusRule, radiusFactor, sweepDegrees, rotationDegrees, decoration, color, strokeWidth.
  - Non-bond pair: axisMode, curvature, decoration, color, strokeWidth.
- The panel remains generic and its public API is unchanged. Hidden keys are calculated before the first run and after restoring selection on every reapply. Changing axisMode therefore refreshes visibility for the next draw. An empty callback shows every parameter.
- Reused the existing value readers, scene snapshots, undo entry and path creation/validation. No new source files, dependencies or undo mechanism; no project regeneration required.
- No existing test expectations or registry size/key expectations were changed. All 35 existing test bodies were checked against HEAD and remain identical; only the uncommitted diagnostic probe was replaced.
- Pre-existing user changes outside the task were preserved. No edits to `install/users/**`, `Vendor/**`, `src/Domain/**` or `src/Core/Undo/**`.

## Tests and verification

Six regression tests were added:

1. `OperatorRedoPanelTests.CycleEveryRelevantParameterChangesAllArrows`: exact five hidden keys, original selection at relevance evaluation, curvature changing resolved arc midpoints/sweep, decoration, RGB color through `style.color`, stroke width, all three arrows, and one undo entry.
2. `OperatorRedoPanelTests.AxisSwitchRefreshesBondAndTwoEndParameters`: Auto bond -> DefectZ -> Bond refreshes the hidden set; non-bond curvature changes sweep; one undo entry.
3. `OperatorRedoPanelTests.OperatorWithoutRelevanceRuleStillRunsAndReapplies`: optional callback compatibility.
4. `SceneOperatorRegistryTests.CurvatureChangesCycleSweepAndBondModeShowsSweepInstead`: Polish label, Float range/default, C_3 sweep at curvature 1, bond sweep visible and curvature hidden.
5. `SceneOperatorRegistryTests.SelectionModeIgnoresStaleAtomsAndKeepsVacancyPairsNonBond`: shared mode decision agrees with generated bond geometry and vacancy relevance.
6. `SceneCurvedArrowTests.CurvatureClampsAndDefaultsForCyclesAndNonBondPairs`: negative/zero/high/nonfinite curvature values.

Static verification completed: `git diff --check`, unchanged public API and original test-body comparisons, call-site review, and line counts (all touched `.cpp` files below 500 lines).

**No build or test executable was run**, as required by task 73; the caller builds and supplies errors. Suggested test filter after building:

```text
DefectStudioTests.exe --gtest_filter=OperatorRedoPanelTests.*:SceneOperatorRegistryTests.*:SceneCurvedArrowTests.*:SceneCurvedArrowUndoTests.*
```

Repository graph maintenance: `graphify update .` attempted twice (AST-only). The captured retry exited with code 1: `[graphify watch] Rebuild failed: [WinError 5] Odmowa dostępu` (Access denied), after warning that `pytest-cache-files-yly255eu` could not be scanned. The tool did not identify the path causing the rebuild failure. The saved `graph.json` and `manifest.json` timestamps remain unchanged, so the graph was not refreshed. Captured output: `graphify-out/.task73-update.log`. No force rebuild or permission changes were made.
