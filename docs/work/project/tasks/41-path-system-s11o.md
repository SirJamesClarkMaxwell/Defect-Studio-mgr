# Task 41 S11o: the gradient gets a ramp, not a list

## Goal

S11n gave the gradient an editor: a vertical stack with one labelled block per stop - Position,
Color, Alpha, Remove - repeated down the panel. It is correct and it is unusable. The thing being
edited is a picture, and a column of numbers makes the user interpolate in their head.

Replace it with a Color Ramp in the shape everyone already knows from Blender: a horizontal bar
showing the gradient itself, the stops as markers on the bar that are dragged to move, add and
remove buttons, and the selected stop's position, colour and alpha as fields underneath.

## The contract, already written

`src/Presentation/Panels/ScenePathGradientRamp.hpp`:

- `DrawGradientRamp(id, gradient, selectedStop)` returning `GradientRampResult`.
- `SampleGradientAt(gradient, position)` and `InsertGradientStopInWidestGap(gradient)` - pure, and
  declared there precisely so the half of the widget that has no ImGui in it is testable.

Read the contract block in full before starting. The parts that are easy to get wrong are written
down there: the stops come back sorted, finite and in `[0,1]`; two stops may share a position and
must not be merged or nudged apart; the selection follows its stop when a drag reorders the list;
removing the last stop disables the gradient rather than leaving it enabled and empty.

`SampleGradientAt` must agree with `SampleStrokeColor` in `PathStrokeMesher.cpp` - clamped at both
ends, no extrapolation. A bar that disagrees with the render is worse than no bar.

The `ponytail:` note says there is no interpolation mode, and why. Do not add one.

## Files to create or change

- `src/Presentation/Panels/ScenePathGradientRamp.cpp` - new
- `src/Presentation/Panels/ScenePathEditorWidget.cpp` - delete the per-stop stack and call the ramp;
  map `dragStarted` / `dragEnded` onto `BeginScenePathStyleDrag` / `CommitScenePathStyleDrag`, and
  keep `selectedStop` across frames

A new `.cpp` needs `scripts/Windows/GenerateProjects.bat`, which you cannot run. Say so in your
report.

## Files that must NOT be touched

- `src/Presentation/Panels/ScenePathGradientRamp.hpp` - written, it is the contract
- `src/Presentation/Panels/ScenePathEditorWidget.hpp` - unchanged; the gradient field stays as it is
- `src/Renderer/` - the sampler and the mesher are correct
- `src/IO/` - no format change
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. `SampleGradientAt` agrees with `SampleStrokeColor` for the same gradient at several positions,
   including before the first stop and after the last.
2. `SampleGradientAt` on an empty or disabled gradient returns opaque white.
3. `InsertGradientStopInWidestGap` puts the new stop in the widest gap and gives it the colour the
   gradient already showed there, so the picture does not jump.
4. `InsertGradientStopInWidestGap` on an empty gradient produces one stop at 0.5.
5. After any widget operation the stop list is sorted, finite and within `[0,1]`.
6. Two stops at the same position survive as two stops: not merged, not nudged apart.
7. Dragging a marker past its neighbour keeps `selectedStop` on the stop that was grabbed.
8. `selectedStop` is clamped into range, and is -1 exactly when there are no stops.
9. Removing the last stop leaves `enabled == false` and does not leave an enabled empty gradient.
10. A whole marker drag is one undo entry; adding or removing a stop is its own single entry.
11. The per-stop stack from S11n is gone from `ScenePathEditorWidget.cpp`, not merely hidden.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. This is `Presentation` only.
- `.cpp` files stay under ~500 lines. `ScenePathEditorWidget.cpp` is 439 today and this removes
  from it.
- The bar is drawn with ImGui's draw list; a stop marker is an `InvisibleButton` plus drawing, the
  way the side panel's resize grip already does it.
- Do not build, do not run tests, do not commit. The verifying session does all three.
