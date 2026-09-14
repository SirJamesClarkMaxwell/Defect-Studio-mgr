# Task 25a fix: Ctrl+Z / Ctrl+Y start a modal axis drag instead of undoing

## Goal
With an atom (or free label / scene arrow) selected and the viewport hovered, Ctrl+Z toggles a modal
Z-axis drag and Ctrl+Y a Y-axis drag, and nothing is undone. After this fix, a chord with Ctrl or Alt
held never starts or toggles a modal axis drag / axis lock, and Ctrl+Z / Ctrl+Y reach `edit.undo` /
`edit.redo` (keybindings.yaml) and undo/redo as before.

## Root cause (known part)
Every modal axis key check reads `ImGui::IsKeyPressed(ImGuiKey_X/Y/Z, false)` with no modifier check:
- `src/Presentation/Panels/ViewportGizmo.cpp` (~356, ~396, ~556, ~632)
- `src/Presentation/Panels/ViewportLabelGizmo.cpp` (~385)
- `src/Presentation/Panels/ViewportSceneArrowGizmo.cpp` (~199, ~224, ~578)

## To investigate (unknown part)
Why `edit.undo` does not run at all on that Ctrl+Z (no undo in the log). Trace how the keymap dispatches
`Ctrl+Z` (KeymapResolver / CommandService / context) and whether an active gizmo drag, ImGui capture
(`WantCaptureKeyboard`, `IsAnyItemActive`) or a context condition blocks it. Fix the root cause once in the
shared place, not per call site. If undo is blocked only because the drag had started, say so and fix
nothing extra.

## Use the project's input layer, not raw ImGui keys
The project has its own `KeyCode` (`src/Core/Utils/KeyCodes.hpp`), `KeyModifiers` / `HasModifier`
(`src/Core/Input/KeyChord.*`) and `Input::GetCurrentKeyModifiers()` / `Input` key queries
(`src/Core/Utils/Input.hpp`). The modifier guard must use these (e.g. require
`Input::GetCurrentKeyModifiers()` to have neither Ctrl nor Alt). Where `Input` already offers an equivalent
of `ImGui::IsKeyPressed` for `KeyCode::X/Y/Z` with the same edge semantics, switch the modal axis checks to
it; if it does not, keep the ImGui press query and add only the `Input`-based modifier guard - do not grow
the `Input` API for this.

## Files to create or change
- The three gizmo files above: one shared modifier guard (e.g. a small `static` helper per file or one in an
  existing shared Viewport header if one exists) applied to every modal axis key check.
- Only if the investigation proves a separate cause: the keymap/command dispatch file that blocks it.
- A GoogleTest only if the dispatch fix is in testable non-ImGui code.

## Files that must NOT be touched
- `SceneObjectsSnapshotCommand.*`, `UndoStack`, `ICommand`, atom edit commands, `RendererLayer.*`.
- `keybindings.yaml` (Ctrl+Z / Ctrl+Y bindings stay).

## Acceptance criteria
1. Release `DefectStudioTests` green (397 passed / 2 known skips or more if a test is added).
2. Manual: select an atom, hover viewport, Ctrl+Z undoes the last edit with no axis line appearing; plain Z
   still starts the modal Z drag.

## Constraints
- Layer rules from `AGENTS.md`; no exceptions in render paths; `.cpp` under ~500 lines where already so.
- Do not commit. The build cannot run in your sandbox (MSVC FileTracker E_ACCESSDENIED) - do not try;
  the calling session builds and tests.
