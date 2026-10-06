# Task 43: layout toggles - four live bugs

Found by hand-testing task 42 in the running app. Fix all four.

## Bug 1: `Ctrl+`` focuses the Terminal instead of toggling it

`keybindings.yaml:523-528` binds `Ctrl+`` to `terminal.focus`, which is a `FocusPanelCommand`
(`EditorLayer.cpp:342-368`): `SetVisible(true)` + `SetWindowFocus`. Pressing it again does nothing
visible. VSCode's `Ctrl+`` hides the terminal again.

Make it toggle: visible **and** already focused -> hide; otherwise show and focus. Do not delete
`terminal.focus`; other bindings and the command palette may reference it. Add a toggle command and
rebind the chord to it.

## Bug 2: `Ctrl+J` only ever hides the Scene Outliner

The bottom region is expected to cover every panel docked below the viewport, not one of them.
Diagnose before fixing - it is probably the same root cause as bug 4, but confirm rather than
assume. Possibilities worth checking: panels sharing one dock node as tabs, panels whose region was
never observed because they were closed at the time, and `IsVisible()` filtering more than intended.

## Bug 3: `Ctrl+Shift+E` does not reopen a closed Project Tree

`FocusPanelCommand::Execute` already calls `SetVisible(true)` before `SetWindowFocus`, and its
description claims "reopening it first if it was closed" - so the intent is there and something
defeats it. Find out what actually happens: whether the command executes at all when the panel is
hidden, whether `SetVisible(true)` sticks, and whether `ImGui::SetWindowFocus` is being called at a
point in the frame where the window does not exist yet. Fix the root cause in `FocusPanelCommand`
so every panel bound to a focus shortcut reopens, not just the Project Tree - `Ctrl+Shift+O`
(Scene Outliner) and the rest go through the same class.

## Bug 4: the region toggles work exactly one round trip, then stop

This is the important one, and the cause is a hole in the task-42 contract, not in your
implementation of it.

`EditorLayer::updateDockRegionPanelTitles` recomputes each region from the live dock nodes every
frame. But hiding the last panel in a dock node makes ImGui merge that node away and give its space
to the sibling. So after one `Ctrl+B`: the left node is gone, the panel that was hidden reports no
region at all, and some other panel can slide into the vacated space and be classified `Left`
instead. The eager

    if (m_LeftDockRegion.IsHidden() && !m_LeftDockTitles.empty())
        m_LeftDockRegion.Forget();

then wipes the restore set, and the region is dead for the rest of the session.

The fix is the new `DockRegionTracker` in `src/Presentation/PanelDockRegions.hpp`: a sticky
title -> region map that is only ever written for panels that are visible AND docked, and never
erased. Region membership for a toggle comes from `RegionOf(title)`, not from a rectangle
classified this frame. Drop the auto-`Forget` heuristic entirely - `Forget()` stays on the class
but nothing calls it per frame.

## Files to create or change

- `src/Presentation/PanelDockRegions.cpp` - implement `DockRegionTracker`.
- `src/Presentation/EditorLayerDockRegions.cpp` - use the tracker; remove the auto-`Forget`.
- `src/Presentation/EditorLayer.hpp` / `EditorLayer.cpp` - the terminal toggle command and the
  `FocusPanelCommand` fix.
- `install/users/default/config/keybindings.yaml` - rebind `Ctrl+``.

## Files that must NOT be touched

- `src/Presentation/PanelDockRegions.hpp` and `tests/Presentation/PanelDockRegionsTests.cpp` - the
  contract, now 12 cases. If a signature or expectation looks wrong, stop and say so.
- `src/Domain/`, `src/IO/`, `src/Renderer/`, `src/ScientificRuntime/`.
- `src/Presentation/ImGuiLayer.cpp`.

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=PanelDockRegionsTests.*` - 12/12 pass, contract files
   byte-identical.
2. Full Release suite: 679 passed / 2 skipped.
3. `Ctrl+B`, `Ctrl+J`, `Ctrl+Alt+I` survive being pressed ten times in a row, hiding and restoring
   the same set every time.
4. A panel dragged to another edge afterwards belongs to that edge's toggle.
5. `Ctrl+`` hides a visible focused Terminal and shows a hidden one.
6. `Ctrl+Shift+E` opens the Project Tree when it is closed; `Ctrl+Shift+O` does the same for the
   Scene Outliner.
7. No `keymap.register.conflict` warnings at startup.

## Constraints

- Same layer boundaries as task 42 (`AGENTS.md`). Panel visibility is Presentation's own state.
- `.cpp` files stay under ~500 lines; `EditorLayer.cpp` is already 1918 - put new code in
  `EditorLayerDockRegions.cpp` unless it is a fix inside an existing `EditorLayer.cpp` function.
- Do not build - MSBuild fails in the sandbox (MSVC FileTracker `E_ACCESSDENIED`). I build Release
  and run the suite, and will send you any compile errors.
- Do not commit.
- Bugs 2 and 3 need diagnosis, not a guessed patch. Say in your report what the actual cause turned
  out to be for each.
