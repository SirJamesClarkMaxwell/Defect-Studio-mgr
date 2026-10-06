# Task 27: menu bar

## Goal

The Widok menu stops being a flat list of every registered panel and becomes one submenu per
category, and the menu bar stops being four near-empty menus by generating a command menu from the
command registry's own metadata. `CommandMeta` already carries `name`, `category` and
`description`, so every command registered anywhere in the app becomes discoverable without a
second hand-maintained list to keep in sync.

## Files to create or change

- `src/Presentation/MenuBarModel.cpp` - replace the stub. Pure grouping/sorting logic, no ImGui.
- Every panel class under `src/Presentation/Panels/` that is registered as a panel - override
  `GetCategory()`. Mechanical; the table below is the filing.
- `src/Presentation/EditorLayer.cpp` - `renderViewMenu` draws the grouped panels and keeps the
  existing "Klonuj panel" submenu (group that one the same way). Add a command menu built from
  `BuildCommandMenuGroups`, executed through the `executeCommand` lambda `renderEditMenu` already
  has - lift it so both menus use one copy rather than a second one.

## Filing

| Category | Panels |
|---|---|
| `Scene` | Renderer/viewport, Scene outliner, Object properties, Bond settings |
| `Structure` | Structure hub, New structure wizard, Structure creation tabs, Supercell builder, Materials collection, Element catalog, Periodic table, Displacement comparison |
| `Analysis` | Electronic structure, Occupation diagram, Group theory, Calculation summary |
| `Project` | Project tree, Text editor, Export image |
| `Console` | Terminal, Calculator console, Logging, Task monitor, Progress monitor |

Anything not listed keeps the default `Other` and still shows up, in the last group. Settings is
already reachable from Narzedzia > Preferencje; file it under `Other` rather than inventing a
category for one panel.

## Files that must NOT be touched

- `src/Presentation/MenuBarModel.hpp` and `src/Presentation/Panels/IPanel.hpp` - the contract.
- Everything under `tests/`.
- `src/Core/Commands/` - the registry already exposes what this needs. If it does not, say so
  rather than changing it.
- Anything under `src/Domain/`, `src/IO/` or `src/Renderer/`. This task is the menu bar.

## Acceptance criteria

1. `tests/Presentation/MenuBarModelTests.cpp` passes in full. Nine cases, eight currently failing.
2. Every other existing test still passes.
3. Widok lists panels grouped under the five category submenus, every registered panel appears
   exactly once, and toggling one still shows/hides it.
4. The command menu lists commands grouped by their registered category, shows each command's
   keybinding where the keymap has one, and choosing one executes it.
5. A command with no category still appears, under "Inne".

## Constraints

- Layer boundaries in `AGENTS.md` are hard. `Presentation` collects intent; a menu item runs a
  command through `CommandRegistry`, it does not reach into another layer's state directly.
- `MenuBarModel.cpp` stays free of ImGui - it is grouping logic, which is why it is testable.
- `.cpp` files stay under ~500 lines. `EditorLayer.cpp` is already large; if the menu rendering
  pushes it over, split the menu bar into its own file and re-run
  `scripts/Windows/GenerateProjects.bat`.
- Do NOT run a build or the tests - the MSBuild toolchain is not reachable from the Codex sandbox.
