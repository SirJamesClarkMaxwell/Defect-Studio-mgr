# Task 112b: Shift+D - duplicate in place, move, one undo step, Esc removes the copies

Plan: `docs/work/project/plans/2026-10-13-structure-building-gizmo.md` (slice 1). Decision (Claude, user away,
recorded in the plan): Esc / right click during the move removes the pending copies.

## Goal

Building a cell by copying atoms must be one gesture, Blender style:
- `Shift+D` over the viewport with atoms (and/or scene objects - same set the existing `Ctrl+D` handles)
  selected: copies are created **exactly at the originals** (no 0.5 A offset) and selected, and the modal move
  starts immediately (lattice fractions from 112a work: `Shift+D X 1/4 Enter`).
- During that move `R` / `S` switch the operation (if the modal core already switches ops on G/R/S keys, reuse it;
  otherwise add switching for this flow only and say so).
- Enter / left click: copies stay where moved. **One** undo step removes them (duplicate + move grouped).
- Esc / right click: the copies are removed (no coincident atoms left behind) and the undo history is as before
  the `Shift+D` (no dangling "duplicate" entry).
- Existing `Ctrl+D` behaviour stays exactly as it is (offset copy, then move; Esc keeps copies).

## Where things are (read first)

- `src/Presentation/Panels/ViewportInteraction.cpp:64` (`duplicateMovePending`, Ctrl+D -> modal G next frame).
- `src/Renderer/Commands/RendererAtomEditCommands.cpp:224` (`DuplicateSelectedAtomsCommand`, fixed 0.5 A offset).
- `src/Presentation/Panels/ViewportModalTransform.cpp` (modal begin/commit/cancel, `CommitTransform`).
- Command/undo services: `CommandRegistry`, `CommandService`; search for an existing undo grouping/compound
  command before writing one. Scene-object duplicate path: the shortcut handler `Ctrl+D` uses for them.
- Keymap: check `Shift+D` is free in the viewport context (search the keymap / shortcut tables).

## Design

- Add an offset parameter to the duplicate command (default = today's 0.5 A; `Shift+D` passes zero) instead of a
  second command class.
- `RendererWindowState` gets a flag for "move belongs to a pending in-place duplicate" (next to
  `duplicateMovePending`). On modal confirm: group duplicate + transform into one undo entry (existing grouping if
  any; otherwise the smallest compound mechanism, documented). On cancel: undo the duplicate through the command
  service and drop its redo entry.
- No change to the modal math (`ModalTransform.*`) unless op switching truly needs it there.

## Files to create or change

- `src/Presentation/Panels/ViewportInteraction.cpp`, `src/Presentation/Panels/ViewportModalTransform.cpp`
- `src/Renderer/Commands/RendererAtomEditCommands.{hpp,cpp}` (offset parameter), `src/Renderer/RendererWindowState.hpp` (flag)
- the scene-object duplicate path if it needs the same zero offset
- keymap/command registration files if `Shift+D` is registered there
- tests: `tests/Renderer/...` next to existing duplicate/atom-edit command tests (find them)

## Files that must NOT be touched

- `src/Presentation/Panels/Phonon*`, `src/Presentation/EditorLayer*`, `src/Presentation/PanelDockRegions*`, `src/IO/PhononPanelSettingsIO*`,
  `src/IO/AnalysisDatasetIO*` (other tasks)
- `src/Renderer/Scene/ModalTransform.*` unless op switching requires it (then say why)
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-112b-duplicate-in-place.md`

## Acceptance criteria

1. Test: duplicate command with zero offset puts copies at the source positions (same fractional coords), indices of
   the copies are selected; default offset unchanged (existing tests untouched).
2. Test: duplicate + transform committed as a group -> one undo restores the original atom list; cancel path -> atom
   list and undo stack depth equal to before.
3. `Ctrl+D` code path unchanged in behaviour.
4. Report: files changed, how grouping/cancel is done, key conflicts found, anything not done.

## Constraints

- Edits go through commands with undo (no direct domain mutation from Presentation).
- PowerShell built-ins only; do not commit; if MSBuild cannot write `build/`, say so. Regenerate projects only if you
  add files (PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`).
