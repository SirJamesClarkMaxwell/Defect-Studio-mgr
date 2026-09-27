# Task 46: G / R / S over the elements selected in Edit Mode

Second slice of the Edit Mode stage. The first slice (task 45) gave the session, the whiskers and
element picking. This one makes the selected elements move.

## Goal

With Edit Mode on and elements selected, `G`, `R` and `S` translate, rotate and scale those elements
exactly the way they already work on whole objects: same constraints, same snapping, same numeric
entry, same `Escape` to cancel, and one undo entry per drag.

## What already exists - almost all of it

This task adds one capture and one apply. Everything else is already built and must be reused:

- `src/Renderer/Scene/ModalTransform.hpp` - the session, the delta, constraints, snapping, numeric
  entry, pivot. **Do not touch it and do not compute a delta yourself.**
- `src/Renderer/Scene/SceneTransform.hpp` - `SceneTransformSelectionSnapshot` already carries one
  vector per object kind, and `CaptureSceneTransformSelection` / `ApplySceneTransformSelection` /
  `RestoreSceneTransformSelection` already dispatch across them. Edit Mode is a sixth kind, not a
  second system.
- `src/Renderer/Scene/SceneTransformPaths.cpp` - **read this first.** It is the same job one level
  up, it shows the `ApplyPathEdit(Context(window), ids, PathRevisionKind::Geometry, ...)` shape that
  gives one undo entry per drag, and the new file should read like its sibling.
- `src/Presentation/Panels/ViewportModalTransform.cpp` - the modal loop. It should need no branch
  for Edit Mode; if it does, keep that branch to a single condition and say so in your report.
- `PathHandleRules::ApplyAutoHandles` - re-derives Auto handles. Call it; do not re-derive anything
  by hand.

## The contracts, already written

- `src/Renderer/Scene/SceneTransformPathElements.hpp` - the three functions. **Read every comment.**
  The local-vs-world section and the "a node in the selection wins over its own handles" rule are
  the two places this task can go quietly wrong, and both are spelled out there.
- `src/Renderer/Scene/SceneTransform.hpp` - `PathElementTransformStart` and the `pathElements`
  member.

## Files to create or change

- `src/Renderer/Scene/SceneTransformPathElements.cpp` - **new.**
- `src/Renderer/Scene/SceneTransform.cpp` - dispatch the three new functions alongside the existing
  per-kind ones, and make `SceneTransformPivotPositions` return the moving elements' world positions
  when `pathElements` is non-empty.
- `src/Presentation/Panels/ViewportModalTransform.cpp` - only if starting a modal from Edit Mode
  needs a condition that does not exist. Prefer none.
- `scripts/Windows/GenerateProjects.bat` must be run - one new `.cpp`.

## Files that must NOT be touched

- `src/Renderer/Scene/ModalTransform.{hpp,cpp}` - the transform maths is done.
- `src/Renderer/Path/PathHandleRules.{hpp,cpp}` - if an aligned handle's twin needs a rule, that
  rule belongs there and is NOT this task. The contract says so and says why.
- `src/Renderer/Path/PathEditSession.{hpp,cpp}` - the session is done and tested.
- `src/Renderer/Scene/SceneTransformPaths.{hpp,cpp}` - Object Mode's path transform is correct.
- anything under `tests/` - a separate session owns the tests.

## Acceptance criteria

1. With a node selected in Edit Mode, `G` moves it, `X`/`Y`/`Z` constrain it, a typed number moves it
   by exactly that much, `Escape` puts it back, and the whole drag is ONE undo entry.
2. The same for a Bezier handle: `G` changes its offset and its node stays put.
3. Selecting a node AND one of its own handles, then translating, moves the handle exactly once -
   the node carries it, the handle is not moved again on top.
4. Selecting a segment translates both its end nodes, and a node shared by two selected segments
   moves once.
5. `R` and `S` act about the selection pivot, and the pivot sits on the selected elements rather
   than on the path's origin.
6. All of the above still land correctly when the path has a NON-IDENTITY object transform - moved,
   rotated AND scaled. This is the criterion that catches the world-vs-local mistake; test it with a
   rotation that is not a multiple of 90 degrees and a non-uniform scale.
7. The path's own transform is unchanged by any of it: Location, Rotation and Scale in the N panel
   read the same before and after.
8. In Object Mode, `G`/`R`/`S` on a whole path behave exactly as they do today.
9. Handles typed Auto are re-derived after the move; Free, Aligned and Vector handles keep their
   stored offsets.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` stays exception-free - `ApplyAutoHandles`
  returns a `Result`, so handle its error rather than letting anything throw.
- `.cpp` files stay under ~500 lines.
- One undo entry per drag. If you find yourself pushing a second, the capture is in the wrong place.
- The design above is APPROVED. Do not stop to ask for confirmation of it - implement it.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
