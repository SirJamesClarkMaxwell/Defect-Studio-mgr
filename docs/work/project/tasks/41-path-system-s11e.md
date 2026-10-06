# Task 41 S11e: path style combos, and the camera-facing arrowhead fold

Two defects from the S11 manual round. They are unrelated and should be two commits.

## Defect 1 - every combo in the path Properties editor is dead

Reported: Profile, Start decoration, End decoration and Depth cannot be changed. Picking a value
in the dropdown appears to do nothing; the field shows the old value again on the next frame.

Cause, verified: `DrawEnumCombo` in `src/Presentation/Panels/ScenePathEditorWidget.cpp` returns
`void` and throws away the `bool` from `ImGui::Combo`. All four call sites in
`DrawScenePathEditor` then try to detect the change with `ImGui::IsItemDeactivatedAfterEdit()`,
which does not fire for `ImGui::Combo` - a combo commits on selection and never goes through the
activate/deactivate cycle that `DragFloat` and `InputText` do. So `changed` stays false, the edit
is never applied, and the next frame re-reads the unchanged style.

The enum name lists and their counts are correct - `StrokeProfile` 3, `PathDecorationKind` 8,
`PathDepthMode` 2 - so only the plumbing is wrong.

### Fix

Make the three `DrawEnumCombo` overloads `[[nodiscard]] bool` returning what `ImGui::Combo`
returned, and use that at the call sites instead of `IsItemDeactivatedAfterEdit()`.

While you are in that function, two things worth looking at - fix them only if you agree:

- `if (changed) return ApplyScenePathStyleEdit(...) != 0;` returns before the Name field is ever
  drawn, so on any frame a style control changed, the name row vanishes from the panel for that
  frame. Drawing the name first, or not early-returning, both fix it.
- `ApplyScenePathStyleEdit` writes every field on every change, which is what the header says it
  does, so this is not a bug - but confirm it still holds after your change.

### Criteria

1. Changing Profile in the panel changes the stored path's profile, and it survives the next
   frame.
2. The same for Start decoration, End decoration and Depth.
3. A multi-path selection applies the combo change to every selected path in one undo entry.
4. The Name row is present on the same frame a combo changed.

Criteria 1-3 are testable through `ApplyScenePathStyleEdit` without ImGui, which is the point of
the widget being split in two. Criterion 4 is visual; state in your report how you checked it.

## Defect 2 - a Camera-facing ribbon's arrowhead is not coplanar with its shaft

Reported with a screenshot: a wide ribbon whose arrowhead has a visible fold/crease where it
meets the shaft, and a shading seam along the shaft.

Cause, diagnosed but NOT yet confirmed by you - verify before fixing:

`src/Renderer/OpenGl/Shaders/path_ribbon.vert` picks the shaft's widening axis at draw time:
- `u_CameraFacing == 0` -> `offsetDir = normalize(aNormal)`, the frame normal. Its comment says
  this is deliberate, because `PathStrokeMesher::AppendDecoration` offsets a ribbon decoration's
  vertices along exactly that axis on the CPU, so the shaft must widen in the same plane or the
  arrowhead would stand perpendicular to its shaft.
- `u_CameraFacing == 1` -> `offsetDir = cross(tangent, normalize(u_CameraPosition - world))`.

`AppendDecoration` (`src/Renderer/Path/PathStrokeMesher.cpp`, around line 241) bakes the head's
vertices along `endpoint.normal` **regardless of profile**, and emits `side = 0` so the shader
does not expand them. So for `CameraFacing` the shaft turns to face the eye while the head stays
pinned to the frame normal, and the two stop being coplanar - exactly the fold reported. `Flat`
is consistent and should be left alone.

### Fix

Your call which side to change; the constraint is that the head and the shaft must widen along
the same axis for every profile, and that the axis for `CameraFacing` depends on the camera and
so cannot be baked at mesh time.

The obvious shape: give the decoration's ribbon vertices a real `side` (-1/0/+1) and a per-vertex
half width so the shader can expand them the same way it expands the shaft, instead of baking the
offset on the CPU. `AppendDecoration`'s own comment explains why `side` is 0 today - a
decoration's half width varies from contour point to contour point and could not be expressed as
one shader-side stroke half width. Note that `StrokeRibbonVertex` already carries per-vertex
fields, so carrying the contour's half width per vertex is available to you; changing that struct
also means changing the vertex attribute layout in `UploadRibbon`
(`src/Renderer/OpenGl/OpenGlPathRenderer.cpp`).

If you find a smaller correct fix, take it and say why.

Do not "fix" this by disabling camera-facing for decorated paths.

### Criteria

5. For each of the three profiles, the endpoint decoration's vertices and the shaft's end ring
   lie in the same plane, checked in a test against `BuildStroke`'s output rather than by eye.
6. Rotating the camera does not change that for `CameraFacing` - the test should evaluate the
   shader's axis rule on the CPU rather than needing a GL context.
7. `Flat` and `Round` produce byte-identical geometry to what they produce today. This is a
   camera-facing fix and must not move anything else.
8. The existing `PathStrokeMesherTests` and `GlPathRenderTests` still pass unchanged.

## Files that must NOT be touched

- Anything under `src/Domain/`, `src/IO/`, `src/App/`, `src/ScientificRuntime/`.
- `src/Renderer/Path/PathPicking.*`, `ScenePathPicking.*`, `PathCommands.*`, `PathStore.*`.
- The contract headers listed in `41-path-system-s11c.md`, except
  `ScenePathEditorWidget.hpp` if and only if defect 1 forces a signature change - and say so
  loudly if it does, because it should not.
- Any existing test file, except to add cases.

## Constraints

- `Renderer` is the exception-free zone: no `throw` under `src/Renderer/`.
- `.cpp` files stay under ~500 lines.
- `near` and `far` are Windows macros.
- Do NOT build and do NOT run tests. The dispatching session does that.

## Manual round

Defect 1: select a path, change every combo, confirm each sticks and each is one Ctrl+Z.
Defect 2: add a Camera-facing ribbon with an Arrow end decoration, orbit the camera, and confirm
the head stays flat against the shaft from every angle. Repeat for Flat and Tube 3D and confirm
nothing about them changed.
