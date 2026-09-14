# Task 25a: scene objects on the global undo stack

First slice of task 25 (unified transform, plan
`docs/work/project/plans/2026-09-13-scene-tools-and-group-theory.md`, step 4, "Undo unification").
The modal G/R/S core is task 25b and is not part of this task.

## Goal
Every edit of pinned measurements, free labels and scene arrows becomes one entry on the app-global
`UndoStack` (the one atom commands already use), so Ctrl+Z / Ctrl+Y undo scene-object and atom edits
together in the order they happened. The separate per-window label stack and its Ctrl+Alt+U /
Ctrl+Alt+Shift+U shortcut are removed.

## Files to create or change
- NEW `src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp` (header and tests are the contract:
  `src/Renderer/Commands/SceneObjectsSnapshotCommand.hpp`, `tests/Renderer/SceneObjectsSnapshotCommandTests.cpp`).
- `src/Core/CoreLayer.cpp`: `GetUndoStackHandle()` (declared in `CoreLayer.hpp`).
- `src/App/ApplicationBootstrap.cpp`: in `initializeCoreLayerSystems`, bind the renderer layer to the handle.
- `src/Renderer/RendererLayer.hpp/.cpp`:
  - `BindUndoStack(WeakRef<UndoStack>)`;
  - `PushPinnedMeasurementUndoSnapshot(window)` keeps its signature and every call site, but pushes a
    `CreateSceneObjectsSnapshotCommand` (resolver = find window by id, onRestored = the existing
    `QueueSceneObjectsModified`) instead of writing the local vectors;
  - new `PushSceneObjectsUndoSnapshot(window, SceneObjectsSnapshot before)` for the three rollback sites
    (`AddBondPinsWithinSet`, `AddAnglePinsWithinSet`, `RemovePinsWithinSet`): capture first, mutate,
    push only when the pin count changed - no more `pinnedMeasurementUndoHistory.pop_back()`;
  - remove `UndoLabelsChange`, `RedoLabelsChange`, `onUndoLabelsRequested`, `onRedoLabelsRequested`
    and their subscriptions.
- `src/Renderer/RendererWindowState.hpp`: remove `pinnedMeasurementUndoHistory` /
  `pinnedMeasurementRedoHistory` (keep `LabelUndoSnapshot`); fix the comments that describe the old stack.
- `src/Renderer/Scene/SceneObjectPersistence.cpp`: drop the two `.clear()` calls on the removed vectors.
- `src/Events/RendererEvents.hpp`: remove `UndoLabelsRequested` / `RedoLabelsRequested`.
- `src/Renderer/Commands/RendererViewportCommands.hpp/.cpp`, `RendererCommandRegistration.cpp`:
  remove `renderer.labels.undo` / `renderer.labels.redo`.
- `install/users/default/config/keybindings.yaml`: remove the two bindings.
- Comments only: stale "Ctrl+Alt+U" / "own stack" mentions in `ViewportLabelGizmo.cpp`,
  `ViewportSceneArrowGizmo.cpp`, `RendererWindowState.hpp`, `RendererLayer.hpp`.

## Files that must NOT be touched
- Gizmo and interaction behaviour (`ViewportGizmo.cpp`, `ViewportLabelGizmo.cpp`,
  `ViewportSceneArrowGizmo.cpp`, `ViewportLabelInteraction.cpp`, `ViewportSceneArrowInteraction.cpp`,
  `ObjectPropertiesPanel.cpp`, `RendererPanel*.cpp`) beyond comment edits - their
  `PushPinnedMeasurementUndoSnapshot` calls stay as they are.
- Atom edit commands, `UndoStack`, `ICommand`, scene persistence format (`SceneObjectsIO`).
- The contract header and tests above.

## Acceptance criteria
1. Release `DefectStudioTests` green (known 2 skips), including `SceneObjectsSnapshotCommandTests`.
2. `DefectStudio` Release builds.
3. `grep -rn "pinnedMeasurementUndoHistory\|pinnedMeasurementRedoHistory\|UndoLabelsRequested\|renderer.labels.undo" src install`
   returns nothing.
4. Manual: add a free label, drag an arrow, delete a pin, change an atom type -> Ctrl+Z four times undoes
   them in reverse order across kinds; Ctrl+Y redoes them; `*` appears after undoing a scene edit.
5. Pressing M over an already fully pinned selection adds no undo entry.

## Constraints
- One drag = one entry: snapshots are still pushed once at drag start, never per frame.
- Never push while `UndoStack::IsApplying()` (a restore must not record itself).
- Known ceiling, not fixed here: opening a project applies saved scene objects without clearing the global
  stack, so an older scene entry for the same window id could restore pre-open objects.
- Layer rules from `AGENTS.md`; `#include "Core/dspch.hpp"` first in every .cpp; no exceptions in render paths.
