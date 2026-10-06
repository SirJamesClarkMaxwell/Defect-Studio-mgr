# Task 41 S11n: the line style and the gradient reach the panel

## Goal

`PathStrokeStyle` has carried `dash` and `gradient` since S5
(`src/Renderer/Path/PathStyle.hpp:137-138`). Both are meshed - `PathDash.cpp` walks the arc length,
`SampleStrokeColor` interpolates the stops - and both round-trip through `scene_objects.yaml`. The
Properties panel has never had a single control for either, so no user has been able to make a
dashed line or a multi-colour stroke.

This slice is editing only. No new rendering, no format change, no mesher change.

## The contract, already written

`src/Presentation/Panels/ScenePathEditorWidget.hpp`:

- `ScenePathStyleEdit` gains `PathDashStyle dash` and `PathGradient gradient`.
- `ScenePathStyleEditState` gains `mixedDash` and `mixedGradient` - true when any field of the dash
  pattern, or any stop of the gradient, differs across the selection.
- `ScenePathLineStyle` - Solid, Dashed, Dotted, Custom - with `ResolveScenePathLineStyle` and
  `ApplyScenePathLineStyle`. Both are pure and both are testable without ImGui; that is the point of
  their being declared.

Read the `ponytail:` note above the enum before implementing: dash-dot is deliberately not offered,
because `PathDashStyle` is one dash and one gap repeated and a pattern list is a change to the
style, the format and `PathDash`'s walk. Do not quietly add it.

## What the panel gains

- A **Line style** combo: Solid, Dashed, Dotted, Custom. Picking a preset writes the lengths;
  editing a length by hand moves the combo to Custom. The combo applies in one shot; the lengths
  are drags and use `BeginScenePathStyleDrag` / `CommitScenePathStyleDrag` like Width does.
- **Dash phase** as a drag, since it already exists and decides where the pattern starts.
- A **gradient** section: an enable toggle, the stop list, and add/remove. Each stop has a position
  in [0,1] of arc length, a colour and an alpha. Adding a stop puts it between its neighbours
  rather than at 0; removing the last stop disables the gradient rather than leaving it enabled and
  empty.
- The stops stay sorted by position and finite. `PathGradient`'s contract is explicit that
  non-finite, out-of-range or decreasing positions are a rejection, not a sort-and-hope, so the
  panel must not be able to produce them. Clamp on edit.

## Files to create or change

- `src/Presentation/Panels/ScenePathEditorWidget.cpp` - `ResolveScenePathStyleEdit`,
  `ApplyScenePathStyleEdit` and `DrawScenePathEditor` all learn the two fields, plus the two new
  pure functions
- `src/Presentation/Panels/ScenePathDevMenu.cpp` - only if a preset is worth adding to see it; a
  dashed dev path is cheap and makes the manual round one click

If `ScenePathEditorWidget.cpp` approaches ~500 lines, split the pure half
(`ResolveScenePathStyleEdit`, `ApplyScenePathStyleEdit`, the line-style functions) from the ImGui
half rather than letting it grow. Say so in your report; a new file needs project regeneration.

## Files that must NOT be touched

- `src/Presentation/Panels/ScenePathEditorWidget.hpp` - written, it is the contract
- `src/Renderer/Path/` - the dash walk and the gradient sampling are correct and already tested
- `src/IO/`, `src/Renderer/Scene/ScenePathPersistence.cpp` - both fields already persist
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. `ResolveScenePathStyleEdit` fills `dash` and `gradient` from the first resolved path.
2. `mixedDash` is true when any dash field differs across the selection; `mixedGradient` is true
   when the stop lists differ in length, or any stop differs in position, colour or alpha.
3. `ApplyScenePathStyleEdit` writes both onto every selected path, in the one undo entry it already
   uses.
4. `ResolveScenePathLineStyle` returns Solid for a disabled dash, Dashed and Dotted for the
   patterns `ApplyScenePathLineStyle` produces, and Custom for anything else.
5. `ApplyScenePathLineStyle` round-trips with `ResolveScenePathLineStyle` for Solid, Dashed and
   Dotted at several stroke widths: applying a preset and resolving it returns that preset.
6. `ApplyScenePathLineStyle` scales with `strokeWidth`, so a dotted pattern on a thick stroke has
   proportionally longer gaps. Assert the ratio, not the numbers.
7. `ApplyScenePathLineStyle(.., Custom, ..)` leaves the dash untouched.
8. Gradient stops written by the panel are always finite, within [0,1] and non-decreasing, whatever
   order they were edited in.
9. Removing the last stop leaves the gradient disabled rather than enabled and empty.
10. Dragging a dash length or a stop position is one undo entry, not one per frame.
11. A path with a gradient enabled and stops still renders with `color` when the gradient is
    disabled - the toggle is not destructive of the stop list.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. This is `Presentation`; it routes edits through the
  existing command/undo path and must not touch `PathStore` directly.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not change the file format. Both fields already have keys.
