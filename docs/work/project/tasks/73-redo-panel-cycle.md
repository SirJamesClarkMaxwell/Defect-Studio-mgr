# Task 73: the redo panel does nothing for a C_3 arrow cycle

Branch `task/70-operator-redo-panel`. Context: `docs/superpowers/specs/2026-10-05-operator-redo-panel-design.md`,
tasks 70-72 in `docs/work/project/tasks/`.

## Bug

User: "I made C_3 arrows, moved things in the panel and it does not work." Reproduced by the probe
test `OperatorRedoPanelCycleProbe.CycleReactsToEveryParameter` at the end of
`tests/Presentation/Panels/OperatorRedoPanelTests.cpp`: for three selected atoms (a cycle) every
Reapply succeeds, but `axisMode`, `radiusRule`, `radiusFactor`, `sweepDegrees` and
`rotationDegrees` change nothing - `AddCurvedArrowThroughSelectedAtoms`
(`src/Presentation/Panels/ScenePathCurvedArrow.cpp`) only reads them in bond (C_2) mode. Only
decoration, colour and stroke width reach the cycle. The design spec promised "radius, curvature,
heads and colors for all n arrows", and "curvature" was never added.

## Fix

1. Add `float curvature` to `CurvedArrowParameters` (`src/Renderer/Path/CurvedArrowParameters.hpp`),
   default = today's `kCurvedArrowFlatness` (0.5): the arc of each non-bond arrow spans
   `curvature x` the angle between its ends (C_3: sagitta about 0.13 of the chord at 0.5). Use it
   instead of the constant for the cycle and for the two-end non-bond (defect z) arrow; clamp to a
   sane range (e.g. [0.05, 1.5]; a sweep of 0 is not a valid arc). Register it in the operator
   schema (`src/Presentation/Operators/CurvedArrowOperator.cpp`) as Float "Wygięcie łuku" with the
   same range, plus the default.
2. Show only the parameters that do something for the current selection, like Blender's operator
   `draw()`. Keep the panel generic (it must not know about arrows): add to `SceneOperator`
   (`src/Presentation/Operators/SceneOperator.hpp`) an optional
   `std::function<bool(const std::string &key, const SceneOperatorValues &, const RendererWindowState &)> isParameterRelevant`
   (empty = every parameter shown). `OperatorRedoPanel` evaluates it while the operator's input
   selection is in place - in `RunAndOpen` before the first run and in `Reapply` after restoring the
   selection, before the re-run - and stores the hidden keys in a new private member; `Draw` skips
   them. (You may add private members to `OperatorRedoPanel.hpp`; do not change its public API.)
   The curved arrow's rule:
   - cycle (>= 3 valid selected atoms): curvature, decoration, color, strokeWidth;
   - two ends in bond mode: axisMode, radiusRule, radiusFactor, sweepDegrees, rotationDegrees,
     decoration, color, strokeWidth;
   - two ends, not bond mode: axisMode, curvature, decoration, color, strokeWidth.
   Put the "is this bond mode / a cycle" decision in ONE helper in `ScenePathCurvedArrow.{hpp,cpp}`
   used by both `AddCurvedArrowThroughSelectedAtoms` and the relevance rule, so they cannot drift.
   Switching `axisMode` for two atoms in the panel must update the shown set on the next frame.
3. Replace the probe test with a real one: for a C_3 cycle through the panel, every parameter the
   relevance rule shows changes the result (style or resolved geometry; colour via `style.color`),
   and the hidden ones are exactly axisMode, radiusRule, radiusFactor, sweepDegrees,
   rotationDegrees. Add one test that curvature changes the C_3 arc sweep and that two atoms in
   bond mode show sweep but not curvature. Do not edit the existing OperatorRedoPanelTests,
   SceneOperatorRegistryTests or SceneCurvedArrowTests expectations (add new tests instead; if the
   registry test checks the exact schema size/keys, update only that expectation and say so).

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`, `src/Domain/**`,
`src/Core/Undo/**`. Polish UI strings with diacritics. Report in
`docs/work/project/tasks/73-redo-panel-cycle-report.md`: files changed, tests, decisions.
