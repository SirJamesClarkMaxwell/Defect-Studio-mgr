# Task 41 S11f: a Flat ribbon can be told which way to face

## Goal

Reported from the S11 manual round: a Flat ribbon added from the dev menu is invisible.

It is not a rendering failure. A Flat ribbon lies in the plane of the transported frame's normal,
and that normal comes from one global `FrameSeed` whose default is `(0, 0, 1)`. For a straight
path the seed projects straight through, so the sheet lies in a plane containing the view
direction and the default camera sees it exactly edge-on.

There is no orientation of the path that fixes this while the seed is global: a tangent along X
gives a normal of Z, and a tangent along Z makes the projection degenerate so the fallback gives
Y - both planes contain the default view direction.

After this task, a Flat ribbon carries its own orientation, it persists with the project, and the
dev preset sets one that is visible.

## Design, already decided

`PathStrokeStyle` gains:

```cpp
// Which way a Flat ribbon's sheet faces. Seeds the transported frame, so it fixes the plane the
// ribbon lies in. Ignored by Round, which is rotationally symmetric, and by CameraFacing, which
// picks its own axis per frame from the eye position.
glm::vec3 ribbonNormal{0.0f, 1.0f, 0.0f};
```

Persisted as an optional `ribbon_normal: [x, y, z]` key. Absent means the default, so every
`scene_objects.yaml` v2 file written before this still loads unchanged and the format version
does NOT go to v3.

## Files to create or change

1. `src/Renderer/Path/PathStyle.hpp` - the field and its comment.
2. `src/Renderer/OpenGl/OpenGlPathRenderer.cpp:167` - the one place a `TessellationSettings` is
   built. It currently passes `{}` for the frame seed. Build the seed from the style instead:
   `FrameSeed::Mode::FixedNormal` with `style.ribbonNormal` for `StrokeProfile::Flat`, and the
   existing default for the other two profiles.
   **Note for the cache:** `ribbonNormal` lives in style, and `PathEvaluationKey` carries both
   revisions, so editing it invalidates the cached geometry correctly. It is nonetheless the
   first style field that changes the *tessellation* rather than only the mesh - say so in a
   comment next to it, because `PathStore`'s revision comment currently claims style only
   invalidates the mesh.
3. `src/Renderer/Scene/ScenePathPersistence.cpp` - read and write the optional key. Follow how
   `profile` and `width` are handled at lines 57-59 / 84 / 239-240. A non-finite or zero-length
   vector on load falls back to the default rather than producing a degenerate frame.
4. `src/Presentation/Panels/ScenePathEditorWidget.{hpp,cpp}` - `ScenePathStyleEdit` gains
   `ribbonNormal` and a `mixedRibbonNormal` flag, `ResolveScenePathStyleEdit` and
   `ApplyScenePathStyleEdit` carry it, and the panel draws a `DragFloat3` for it.
   **Draw the control only when the resolved profile is Flat** - it does nothing for the other
   two and a dead control is worse than no control. When the selection has mixed profiles, draw
   it if any selected path is Flat.
5. `src/Presentation/Panels/ScenePathDevMenu.cpp` - the Flat preset sets a `ribbonNormal` that is
   face-on to the default camera, so "add a Flat ribbon" shows you a ribbon. Say in a comment
   which view you picked it for.

## Files that must NOT be touched

- `src/Renderer/Path/PathFrames.{hpp,cpp}` - `SeedFrame` already does the right thing with a
  `FixedNormal` seed. Do not change the global default; that would move every existing Flat
  path's geometry.
- `src/Renderer/Path/PathPicking.*`, `ScenePathPicking.*`, `PathCommands.*`, `PathStore.*`.
- `src/Domain/`, `src/IO/`, `src/App/`, `src/ScientificRuntime/`.
- The scene object format version. This is an additive optional key, not a v3.

## Out of scope

- A viewport handle for dragging the ribbon's orientation. S13 owns numeric and gizmo editing.
- Per-node or per-segment orientation. One vector per path is the V1 answer; a ribbon that twists
  along its length is a different feature.
- Changing what `CameraFacing` or `Round` do. Neither reads this field.

## Acceptance criteria

1. A Flat path with the default `ribbonNormal` tessellates to frames whose normal is that vector,
   projected perpendicular to the tangent.
2. Changing `ribbonNormal` changes the ribbon's plane, checked against `BuildStroke` output.
3. `Round` and `CameraFacing` geometry is unchanged by any value of `ribbonNormal`.
4. A zero-length or non-finite `ribbonNormal` falls back to the default and never produces a
   non-finite frame.
5. Round-trip: save a path with a non-default `ribbonNormal`, reload, get the same vector.
6. A `scene_objects.yaml` v2 file with no `ribbon_normal` key loads, keeps version 2, and the
   path gets the default.
7. `ResolveScenePathStyleEdit` reports `mixedRibbonNormal` when two selected Flat paths differ,
   and `ApplyScenePathStyleEdit` writes it to every selected path in one undo entry.
8. `MakeDevScenePath` with `StrokeProfile::Flat` produces a path whose ribbon plane contains the
   default camera's up and right axes - i.e. the preset is actually visible. State in your report
   which camera convention you checked against.
9. Editing `ribbonNormal` invalidates the cached geometry, so the change is visible without any
   other edit. A test that stores a cache entry, edits the style and shows the key no longer
   matches is enough.

Build and suite:

10. `scripts\Windows\GenerateProjects.bat` succeeds.
11. Release build of both targets is clean.
12. The full Release suite passes with exactly the two known `DS_PYTHON_CAPI_AVAILABLE=0` skips.

## Constraints

- Layer boundaries from `AGENTS.md` are hard.
- `Renderer` is the exception-free zone: no `throw` under `src/Renderer/`.
- `.cpp` files stay under ~500 lines.
- `near` and `far` are Windows macros.
- Every store mutation goes through `PathCommands`.
- This lands on top of S11e, which is changing `path_ribbon.vert` and
  `PathStrokeMesher::AppendDecoration`. If either is dirty when you start, stop and say so.

## Manual round

Add a Flat ribbon from the dev menu: it is visible. Select it, change `Ribbon normal` in
Properties, watch the sheet turn. Confirm the control is absent for a Tube 3D and a Camera-facing
path. Save, reopen, confirm the orientation came back. Open a project saved before this change
and confirm its paths still load.
