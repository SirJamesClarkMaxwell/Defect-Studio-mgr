# Task 41 S8: path commands, undo, dirty

## Goal
Every path edit becomes an operation that can be undone as one history entry and that marks the
project dirty, instead of a direct `PathStore` mutation. A drag (node/handle move) produces exactly
one undo entry on commit and nothing at all on cancel. Multi-path operations validate the whole
target set before touching anything and report the targets they skipped. The S7 dev add menu stops
mutating the store directly and goes through the new layer, so Ctrl+Z works on a dev-added path.

## Files to create or change
- `src/Renderer/Path/PathCommands.cpp` (new) - implements the header, which is already written.
- `tests/Renderer/Path/PathCommandsTests.cpp` (new) - see acceptance criteria.
- `src/Presentation/Panels/ScenePathDevMenu.cpp` - route the three dev add items through
  `AddScenePath` from the new header instead of calling `SceneSystem::AppendScenePath` directly.

## Files that must NOT be touched
- `src/Renderer/Path/PathCommands.hpp` - the contract. If a signature is wrong, stop and say so.
- Everything else under `src/Renderer/Path/` - S1-S6 are frozen.
- `src/Renderer/OpenGl/**` - the renderer is not involved in this stage.
- `src/Renderer/Commands/**`, `src/Renderer/RendererLayer.{hpp,cpp}` - the undo sink is injected
  by the caller, not reached through the renderer globals. Wiring the real sink into the app is
  deliberately out of scope for S8 (see "Out of scope" below).
- `src/IO/**`, `src/Domain/**`, `src/App/**`.
- Any existing test file.

## Out of scope (deliberate, do not add)
- Registering path commands in `RendererCommandRegistration.cpp` and adding keymap entries.
  Every such command needs a path selection to act on, and `selectedScenePaths` does not exist yet -
  S6 deliberately left it out because selection belongs to S10 (picking) and S11 (Object Mode UI).
  Registering handlers that can never have a target now would be dead code. The registration lands
  in S11/S12 together with the selection it operates on.
- An inverse arc solver (centre/axis/radius/start/sweep -> endpoint node positions). The path model
  stores only `planeNormal` and `signedSweepRadians`; `SetArcParameters` here validates and sets
  exactly those two fields. The numeric editor that moves endpoints to satisfy an authored centre
  and radius is S13.

## Acceptance criteria
All tests live in `tests/Renderer/Path/PathCommandsTests.cpp`, are plain `TEST(...)` cases with a
local `UndoStack`, and must not need a GL context or an ImGui context.

1. `ApplyPathEdit` on a single valid target mutates the stored path, returns it in `applied`, and
   leaves `skipped` empty.
2. `ApplyPathEdit` where `apply` returns a `StructuredError` for one of two targets applies the
   other one, reports the failing target in `skipped` with that error, and leaves the failing
   path byte-identical to what it was before the call.
3. `ApplyPathEdit` where `apply` succeeds but leaves the path failing `ValidatePath` (for example
   it removes a segment without removing a node) skips that target and does not commit the result.
4. When every target is skipped, nothing is pushed on the undo stack and the sink is not called:
   `stack.CanUndo()` is false and the sink's call count is 0.
5. A successful `ApplyPathEdit` pushes exactly one entry; `stack.Undo()` restores the pre-edit
   geometry of every applied path, `stack.Redo()` restores the edited geometry.
6. `PathRevisionKind::Geometry` bumps only the geometry revision, `Style` bumps only the style
   revision (check with `PathStore::RevisionsFor`).
7. `AddScenePath` allocates an id from the window's `SceneRegistry`, inserts the path, pushes one
   undo entry, and `Undo()` leaves the store empty.
8. `DeleteScenePaths` on two ids erases both, and `Undo()` brings both back with their geometry.
9. `DeleteScenePaths` with one live and one unknown id erases the live one and reports the unknown
   one in `skipped`.
10. `ReverseScenePaths` reverses node order and `Undo()` restores the original order.
11. `InsertScenePathNode` on a two-node Line path yields a three-node path and returns a valid new
    element id; `Undo()` restores two nodes.
12. `MoveScenePathNode` with a non-finite position is rejected, changes nothing and pushes nothing.
13. `SetScenePathArcParameters` rejects a sweep of 0 and a normal parallel to the chord (both are
    already rejected by `DeriveArc`), and accepts a legal one.
14. `SetScenePathBinding` sets a `CopyPosition` binding on a node and `DetachScenePathBinding`
    returns it to `Free`; both are undoable.
15. A drag: `Begin`, three `Update` calls with different positions, `Commit` leaves exactly one
    entry on the stack, and `Undo()` restores the pre-`Begin` position - not the second-to-last
    `Update`.
16. A cancelled drag: `Begin`, two `Update` calls, `Cancel` restores the exact pre-`Begin` state
    and `stack.CanUndo()` is false.
17. `Update` after `Commit` or `Cancel` (that is, on an inactive transaction) applies nothing and
    reports every target as skipped.
18. A drag that moves a path node while a free label is also edited between `Begin` and `Commit`
    undoes as one entry and restores both - the snapshot the transaction captured is the whole
    window's scene objects, not just its paths.
19. The full Release suite is green with the two permanent `DS_PYTHON_CAPI_AVAILABLE=0` skips and
    no other skips.

## Constraints
- `Renderer` is the documented exception-free zone: no `throw` in either new `.cpp`. Errors are
  `StructuredError` / `Result<T>`.
- No `std::thread` anywhere. No new dependency.
- `.cpp` files stay under ~500 lines.
- The op layer must not reach the renderer globals (`g_RendererUndoStack`, `g_RendererLayer`).
  The only way an edit reaches history is the `PathUndoSink` in `PathEditContext`; an empty sink is
  legal and means "apply the edit, record no history".
- Atomicity is structural, not a convention: `ApplyPathEdit` runs `apply` on a **copy** of each
  path and only commits copies that both succeeded and pass `ValidatePath`. Do not mutate the
  stored path in place and roll back afterwards.
- Reuse what exists. `InsertNode`, `DeleteNode`, `ReversePath` are already in
  `Renderer/Path/PathTopology.hpp`; `ValidatePath` and `DeriveArc` are in `PathEvaluator.hpp`;
  the whole-window snapshot and its undo command are in
  `Renderer/Commands/SceneObjectsSnapshotCommand.hpp`. Do not write a second copy of any of them.
- Run `scripts/Windows/GenerateProjects.bat` after adding the new files. Do not build - the
  sandbox cannot build this project; this session builds and tests.

## Manual round (user, after the stage)
Add a dev path from the context menu, press Ctrl+Z, confirm it disappears and Ctrl+Y brings it
back. This is the first stage where a path edit is undoable.

Result (2026-09-26): passed. Ctrl+Z removes a dev-added path, Ctrl+Y brings it back.
