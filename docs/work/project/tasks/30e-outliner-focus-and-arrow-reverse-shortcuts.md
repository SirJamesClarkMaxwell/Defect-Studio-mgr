# Task 30e: two keyboard shortcuts - focus the Scene Outliner, reverse an arrow

Branch: `task/30e-scene-shortcuts`. Small and bounded; both are reuse, not new machinery.

## Goal

1. A keyboard shortcut brings the Scene Outliner panel to focus, reopening it first if it was
   closed.
2. A keyboard shortcut reverses the selected scene arrow or arrows - swapping which end is the
   head, and with it the atom anchors.

## Both of these already exist as code. Only the binding is missing.

- **Focus.** `EditorLayer::initializePanelsIfNeeded` (`EditorLayer.cpp:594-608`) registers
  `editor.focus_project_tree` as a `FocusPanelCommand` over the Project Tree panel id, and
  `install/users/default/config/keybindings.yaml` binds it to `Ctrl+Shift+E`. Register
  `editor.focus_scene_outliner` the same way over the Scene Outliner panel's id, and add the
  binding beside the existing one. Do not write a new command type - `FocusPanelCommand` is exactly
  this, and its "reopen if closed" behaviour is what the user wants.
- **Reverse.** `ReverseSceneArrow(RendererWindowState::SceneArrow &)` already exists
  (`SceneArrowOperations.cpp:184`, declared in `SceneArrowEditorWidget.hpp:119`) and, as of task
  30d, already swaps the two atom anchors along with the endpoints. It is currently reachable only
  from the properties panel. Wire it to a shortcut that applies to every selected arrow.

## Design

**Chords.** `Ctrl+Shift+O` for the outliner - it sits beside `Ctrl+Shift+E` for the Project Tree
and the letter matches the panel. `Alt+R` for reverse, applied to the current arrow selection.
Check `keybindings.yaml` for a collision before committing to either; if one is taken, pick the
nearest free chord and say which and why in your report rather than stealing an existing binding.

**Scope of reverse.** It acts on `windowState.selectedSceneArrows` - all of them, not just the
first. An empty arrow selection does nothing, quietly; it is a shortcut, not a dialog. The change
goes on the undo stack through the same snapshot helper the properties-panel button already uses
(`PushPinnedMeasurementUndoSnapshot`) - one snapshot for the whole batch, not one per arrow.

**Where reverse gets registered.** It needs the active `RendererWindowState`, which the Editor-level
commands in `EditorLayer.cpp` do not hold. Find where other viewport-scoped scene shortcuts are
handled and put it there, in the same layer, rather than reaching for the window state from a place
that does not already have it. Say in your report where you put it and what the precedent was.

## Files to create or change

- `src/Presentation/EditorLayer.cpp` - the focus command registration.
- `install/users/default/config/keybindings.yaml` - both bindings.
- Whichever file already owns viewport-scoped scene-object shortcuts - the reverse binding.
- `tests/` - see Acceptance criteria.

## Files that must NOT be touched

- `src/Domain/`, `src/App/`, `premake5.lua`.
- `ReverseSceneArrow` itself - its behaviour is already correct and tested. Call it, do not change
  it.
- `FocusPanelCommand` - likewise.
- Any existing chord in `keybindings.yaml`.

## Acceptance criteria

1. A GoogleTest reverses a selection of two arrows through whatever entry point the shortcut calls
   and asserts both arrows swapped ends and swapped anchors, and that one undo restores both.
2. A GoogleTest asserts an empty arrow selection is a no-op and pushes nothing onto the undo stack.
3. A GoogleTest (or an assertion in an existing keymap test) asserts the two new bindings parse and
   that neither chord duplicates an existing one.
4. `scripts/Windows/Build.bat --config Release` builds both targets with zero errors and zero
   warnings. 2 skipped tests expected (`DS_PYTHON_CAPI_AVAILABLE=0`).

## Constraints

- Layer boundaries from `AGENTS.md` are hard: user actions go through `CommandRegistry` and the
  keymap, not an ad-hoc key check in a panel's draw function.
- Only the main thread mutates state visible in the project or UI.
- `.cpp` files stay under ~500 lines.
- Do not build or run anything needing an approval step. Report what you changed; the dispatching
  session builds, runs the suite and exercises the app.
