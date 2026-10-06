# Task 41 S11r: the ramp reads at a glance

## Goal

The gradient ramp works. The manual round asked for two things: the add and remove buttons should
sit next to the enable toggle rather than below the bar, and the widget should read better.

## What changes

The contract block in `src/Presentation/Panels/ScenePathGradientRamp.hpp` now states the layout:

- `+` and `-` on the same row as the enable toggle, above the bar. They are what you reach for
  first and they were the furthest thing away.
- Markers carry their own colour, so the bar says what each stop is without being read twice.
- The selected marker is drawn distinctly, so it is obvious which stop the fields underneath belong
  to.

Everything else about the widget stays. The contract's rules on sorting, coincident stops,
selection following a reordering drag, and the sampler agreeing with the renderer are unchanged and
must keep holding.

## Files to create or change

- `src/Presentation/Panels/ScenePathGradientRamp.cpp`

## Files that must NOT be touched

- `src/Presentation/Panels/ScenePathGradientRamp.hpp` - the contract
- `src/Presentation/Panels/ScenePathEditorWidget.cpp` - the call site does not change
- `src/Renderer/` - nothing here is a rendering change
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. `+` and `-` are on the enable row, above the bar.
2. A marker is drawn in its stop's colour.
3. The selected marker is visually distinct from the others.
4. Every behavioural rule the header states still holds: stops sorted, finite and in `[0,1]`;
   coincident stops preserved; selection follows the grabbed stop through a reordering drag;
   `selectedStop` clamped, and -1 exactly when there are no stops; removing the last stop disables
   the gradient.
5. A whole marker drag is still one undo entry; add and remove are still one each.
6. `SampleGradientAt` and `InsertGradientStopInWidestGap` are untouched - this is a drawing and
   layout change, and their existing tests must keep passing unmodified.
7. Hit-testing still matches what is drawn: a marker is grabbed by clicking where it appears,
   including when two markers sit at the same position.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Presentation` only.
- `.cpp` files stay under ~500 lines.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
