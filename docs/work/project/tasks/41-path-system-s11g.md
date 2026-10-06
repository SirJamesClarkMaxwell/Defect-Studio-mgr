# Task 41 S11g: one Ctrl+Z per drag, not one per frame

## Goal

Dragging Width, Alpha, Color or Ribbon normal in the path Properties panel currently pushes one
undo entry per rendered frame, so a one-second drag needs dozens of Ctrl+Z presses to walk back and
the user reports the sliders as "not undoable at all". After this task one drag - from the moment
the widget is activated to the moment it is released - is exactly one undo entry, and undoing it
returns every selected path to the value it had before the drag started.

The bug is at `src/Presentation/Panels/ScenePathEditorWidget.cpp:166`: `if (changed)` is true on
every frame ImGui reports a change while a slider is held, and `ApplyScenePathStyleEdit` routes
each of those through `MakeWindowPathEditContext`, which carries the undo sink.

## The shape of the fix

Do not invent a mechanism. The repo already has this exact pattern: `ViewportModalTransform.cpp:264`
captures one `SceneObjectsSnapshot` into `windowState.modalTransformSceneObjectsBefore` when a G/R/S
drag starts and pushes it once on commit. `MakeSilentPathEditContext` (ScenePathOperations.hpp)
already gives "apply, record nothing", and `PathUndoSink` already takes a `SceneObjectsSnapshot
before` argument, so a snapshot captured earlier can be pushed later.

`RendererWindowState::scenePathStyleEditBefore` and the `BeginScenePathStyleDrag` /
`CommitScenePathStyleDrag` pair in `ScenePathEditorWidget.hpp` are already written and are the
contract. Implement them, and rewire `DrawScenePathEditor` to use them.

In `DrawScenePathEditor`, the ImGui side of it:

- `ImGui::IsItemActivated()` after a widget -> `BeginScenePathStyleDrag`.
- a reported change while a drag is open -> `ApplyScenePathStyleEdit(..., /*recordUndo=*/false)`.
- `ImGui::IsItemDeactivatedAfterEdit()` -> `CommitScenePathStyleDrag`.
- A control that is not a drag at all - the combos (Profile, decorations, Depth) - still applies
  with `recordUndo = true` in one shot. A combo has no held state; giving it a Begin/Commit pair
  would be ceremony for nothing.

`ColorEdit3` is the awkward one: it opens a picker popup, so activation and deactivation are not on
the same widget. Handle it and say in the report how. `ImGui::IsItemDeactivatedAfterEdit` on the
`ColorEdit3` itself is the documented way, but verify against this ImGui version rather than
assuming.

## Files to create or change

- `src/Presentation/Panels/ScenePathEditorWidget.cpp`

## Files that must NOT be touched

- `src/Presentation/Panels/ScenePathEditorWidget.hpp` - written, it is the contract
- `src/Renderer/RendererWindowState.hpp` - the field is already added
- `src/Renderer/Path/PathCommands.{hpp,cpp}`, `src/Renderer/Path/PathStore.hpp` - the undo sink and
  the store's mutation contract are not part of this defect
- `src/Presentation/Panels/ScenePathOperations.{hpp,cpp}`
- anything under `tests/` - a separate session owns the tests
- anything outside `src/Presentation/Panels/`

## Acceptance criteria

1. `BeginScenePathStyleDrag` captures `windowState.scenePathStyleEditBefore` from the current state
   and is a no-op when a snapshot is already held.
2. `CommitScenePathStyleDrag` pushes exactly one undo entry and clears the snapshot.
3. `CommitScenePathStyleDrag` returns false and pushes nothing when no snapshot is held.
4. `CommitScenePathStyleDrag` returns false and pushes nothing when the scene objects are unchanged
   since Begin - clicking a slider without moving it leaves no history.
5. N consecutive silent applies between one Begin and one Commit produce an undo depth increase of
   exactly 1, whatever N is.
6. Undoing that one entry restores the style of every path that was in the selection, not just the
   first.
7. `ApplyScenePathStyleEdit` with `recordUndo == true` behaves exactly as before, so the existing
   `MultiPathStyleEditUsesOneUndoAndRestoresEveryPath` test still passes unchanged.
8. The combos still apply in one shot with an undo entry each.
9. No new global or file-static state. The in-flight drag lives in `RendererWindowState`, so two
   renderer windows dragging different selections do not share one snapshot.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. This is `Presentation`; it collects intent and routes
  edits through the existing command/undo path. It must not reach into `PathStore` directly.
- `.cpp` files stay under ~500 lines. `ScenePathEditorWidget.cpp` is 170 today; if this pushes it
  near the limit, split the pure half from the ImGui half rather than letting it grow.
- Do not build and do not run tests - the sandbox cannot, and the verifying session does it.
- Do not change any test file or any test expectation.
