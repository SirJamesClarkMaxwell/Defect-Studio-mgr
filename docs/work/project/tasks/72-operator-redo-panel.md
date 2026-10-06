# Task 72: Operator redo panel

## Goal

Add Blender's "adjust last operation" panel. After an Add operation runs, a small panel appears in
the viewport's bottom-left corner with one widget per schema entry. Moving a widget restores the
scene as it was before the operation and runs the operator again with the new values, so the user
tunes radius, sweep, rotation, heads and colour of all n arrows at once and `Ctrl+Z` removes the
whole thing in a single step.

## Files to create or change

- Create: `src/Presentation/Panels/OperatorRedoPanel.cpp` - the panel.
- Modify: `src/Presentation/Panels/ViewportAddMenu.cpp` - the curved-arrow entry runs the operator
  through the panel instead of calling `AddCurvedArrowThroughSelectedAtoms` directly.
- Modify: wherever the viewport's per-frame UI is drawn, call `OperatorRedoPanel::Draw` and
  `PollInvalidation` once per frame. Find the existing owner of the viewport overlay state rather
  than inventing a new one; the panel instance lives next to the other viewport UI state.

## Files that must NOT be touched

- `src/Presentation/Panels/OperatorRedoPanel.hpp` - the contract. Signatures and members are fixed.
- `tests/Presentation/Panels/OperatorRedoPanelTests.cpp` - the contract. Do not change a single
  expectation. If one looks wrong, stop and say so in your report instead of editing it.
- `src/Presentation/Operators/*` - task 71 shipped these.
- `src/Presentation/Panels/ScenePathCurvedArrow.{hpp,cpp}` - finished.
- `tests/Presentation/Panels/SceneCurvedArrowTests.cpp`,
  `tests/Presentation/Operators/SceneOperatorRegistryTests.cpp` - finished.
- Anything under `src/Domain/` or `src/Core/Undo/`. **No new undo API.**

## Acceptance criteria

Every test in `tests/Presentation/Panels/OperatorRedoPanelTests.cpp` passes, unedited:

1. `FiveReapplicationsLeaveOneUndoEntry`
2. `UndoAfterReapplyRestoresThePreOperationScene`
3. `AnUnrelatedUndoEntryClosesThePanel`
4. `AClosedWindowClosesThePanel`
5. `AFailingReapplyClosesThePanelAndKeepsTheObjects`
6. `AFailingFirstRunPushesNothingAndStaysClosed`
7. `ValuesStartAtTheOperatorDefaults`
8. `ReapplyWithoutAnOpenPanelIsRejected`
9. The full Release suite shows no new failures. Known and expected: two
   `DS_PYTHON_CAPI_AVAILABLE=0` skips, and 17 failing `PointGroupAnalysisBridgeTests` caused by a
   `groupy` API change outside this repo.

## Constraints

- Undo: `RunAndOpen` captures `CaptureSceneObjectsSnapshot(window)` into `m_Before`, calls
  `PushSceneObjectsUndoSnapshot(window, copy of m_Before)` exactly once, then runs the operator.
  `Reapply` never touches the stack. The operator's own `execute` suppresses its undo push already
  (`SceneOperationUndo::Suppress`), so there is exactly one entry per session.
  **Push the entry only after the first run succeeded** - a rejected operation must leave the stack
  at the depth it had.
- `m_Before` must survive every re-run: `RestoreSceneObjectsSnapshot` takes the snapshot by value,
  so pass a copy, never `std::move(m_Before)`.
- An Add operation consumes `selectedAtomIndices` / `selectedVacancies`. Capture them in
  `RunAndOpen` and restore them before each re-run, or the second run fails with no selection.
- `PollInvalidation` closes when `window == nullptr` or `undoStack.GetUndoDepth() != m_UndoDepth`.
  Closing never restores anything - a foreign edit made while the panel was open must survive.
- The ImGui body renders from `op.schema` alone and knows nothing about curved arrows:
  `Kind::Float` -> `SliderFloat` between `minimum` and `maximum`, `Int` -> `SliderInt`,
  `Bool` -> `Checkbox`, `Enum` -> `Combo` over `enumLabels`, `Color` -> `ColorEdit3`. Call
  `Reapply` when a widget reports a change and the mouse is released, not on every drag frame if
  that proves too slow - correctness first, then measure.
- Panel chrome: a collapsible header showing `op.label`, bottom-left of the viewport, no title bar,
  no docking. Visible strings are Polish like the rest of the UI; code and ids stay English.
- No exceptions in rendering paths.
- Layer boundaries from `AGENTS.md` are hard.
- `.cpp` files stay under ~500 lines.
- Regenerate projects with `scripts/Windows/GenerateProjects.bat` after adding any new file.
- Do not build. The sandbox denies the file access MSBuild needs; the build and the test run happen
  outside it.
