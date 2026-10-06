# Task 41 transform-4: the rotate and scale gizmos

Step 4 of `docs/work/project/plans/2026-09-26-path-object-transform.md`, and the last of it.

## Goal

The viewport has a translate gizmo. Rotation and scale are reachable only through the modal
keys `R` and `S`. The user asked for the two Blender gizmos by name and showed screenshots: a
rotate gizmo drawn as three coloured arcs inside a white trackball circle, and a scale gizmo drawn
as three handles ending in small boxes plus a circle for uniform scale.

## What this is not

It is not a new transform system. Steps 1 to 3 gave a path a real transform, the modal transform
already computes deltas, applies them and owns the undo snapshot, and the constraint machinery
already resolves an axis against Global or Local orientation. This is an input surface over that.

Anything here that ends up recomputing a delta itself, or pushing its own undo entry, is in the
wrong place. Drive the existing modal transform.

## What to build

- **Rotate gizmo**: three arcs, one per axis, coloured as the existing constraint lines are, drawn
  at the selection's pivot and oriented by the current `TransformOrientation`. Dragging an arc
  starts a modal rotation constrained to that axis. The enclosing circle rotates about the view
  axis.
- **Scale gizmo**: three handles from the pivot ending in small boxes, same colours, plus a centre
  region for uniform scale. Dragging a handle starts a modal scale constrained to that axis;
  dragging the centre scales uniformly.
- **Which gizmo is shown** follows the existing operation state - `RendererWindowState::gizmoOperation`
  is already Translate / Rotate / Scale and is already switched by the toolbar and by the keys.

## Files to create or change

- `src/Presentation/Panels/ViewportGizmo.cpp` - the drawing and the hit-testing
- `src/Presentation/Panels/ViewportModalTransform.{hpp,cpp}` - only if starting a constrained modal
  from a gizmo drag needs an entry point that does not exist yet. Prefer using what is there.

If `ViewportGizmo.cpp` approaches ~500 lines, split the rotate and scale gizmos into their own
`.cpp` and say so; a new file needs `scripts/Windows/GenerateProjects.bat`.

## Files that must NOT be touched

- `src/Renderer/Scene/ModalTransform.{hpp,cpp}`, `SceneTransform.{hpp,cpp}`,
  `SceneTransformPaths.cpp` - the transform maths is done and is not this task's
- `src/Renderer/Path/` - nothing here is about paths specifically; the gizmos act on any selection
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. The rotate gizmo appears at the selection's pivot when the operation is Rotate, and the scale
   gizmo when it is Scale.
2. Both are drawn in the viewport only, clipped to it - the same defect the constraint line had.
3. Dragging an axis arc rotates about that axis; dragging an axis handle scales along it.
4. Dragging the trackball circle rotates about the view axis; dragging the scale gizmo's centre
   scales uniformly.
5. The axis a drag picks respects the current Global / Local orientation, using the existing
   constraint resolution rather than a second copy of it.
6. A whole drag is one undo entry, because it goes through the modal transform that already owns
   the snapshot.
7. Pressing `R` or `S` still works exactly as before, and a gizmo drag and a keyed modal cannot
   both be active at once.
8. The gizmo does not swallow clicks meant for the viewport: a click that misses every handle
   selects or deselects as it does today.
9. It works for every selection kind the modal transform supports, not only paths.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Presentation` collects intent; the transform is
  applied through the existing command and undo path.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
  Other sessions are editing other directories.
