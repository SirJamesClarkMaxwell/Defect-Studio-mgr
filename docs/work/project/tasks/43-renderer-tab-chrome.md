# Task 43: renderer tab chrome - Ctrl+T, Ctrl+W, and the toolbars leave the tab

First slice of `docs/work/project/plans/2026-09-26-renderer-windows-as-tabs.md`. The plan's other
two slices - the owner-key persistence change and free windows surviving save - are NOT in this
task and nothing here may start them.

## Goal

Ctrl+T opens a new empty renderer tab, Ctrl+W closes the one showing, a `+` on the tab bar does
what Ctrl+T does, and the two viewport toolbars are drawn once for whichever tab is showing instead
of once inside every renderer window.

## What already exists - do not build a second one

Read these before writing anything. Most of this task is wiring, not new machinery.

- `RendererEvents::Windows::OpenEmptyRequested` already exists and is already handled by
  `RendererRuntimeOpenCoordinator::onOpenEmptyRequested`, which calls `OpenEmptyRendererWindow`.
  The main menu (`EditorLayerMenus.cpp:82`) and the project tree (`ProjectTreePanel.cpp:780`)
  already publish it. Ctrl+T and `+` are a third and fourth publisher, nothing more.
- Renderer windows already dock into the dockspace's central node
  (`RendererPanel::renderStructureWindow`, the `DockBuilderGetCentralNode` block), and a dock node
  with several windows draws a tab bar. **Tabs already work.** This task does not create them.
- `RendererLayer::RemoveWindow` already closes a window, and `RendererPanel::render` already drains
  a `windowsToClose` list into it.
- `DrawViewportToolbar` and `DrawViewportVerticalToolbar` are already free functions over a
  `RendererWindowState &`. `StructureCreationTabsPanel.cpp:176-178` already calls exactly that pair
  once for its active pane. Neither function changes in this task.
- `ImGui::DockNodeBeginAmendTabBar` / `DockNodeEndAmendTabBar` are in the vendored
  `imgui_internal.h` (line 3719). That is how a `+` gets onto a dock node's tab bar.

## The contracts, already written

- `src/Presentation/Panels/RendererTabChrome.hpp` - `ResolveActiveRendererWindowId`,
  `DrawViewportToolbarOverlays`, `DrawRendererTabBarAddButton`, `RendererTabHoldsContent`,
  `RendererTabCloseCoordinator`. Read every comment in it; the reasoning for each signature is
  there and the comments are part of the contract.
- `src/Events/RendererEvents.hpp` - `RendererEvents::Windows::CloseRequested`.
- `src/Renderer/Commands/RendererViewportCommands.hpp` - `CreateRendererNewWindowCommand`,
  `CreateRendererCloseWindowCommand`.
- `src/Presentation/Panels/RendererPanel.hpp` - now inherits `EventReceiver`, carries a
  `RendererTabCloseCoordinator` and the active tab's viewport rectangle, and
  `renderStructureWindow` takes the active window id.

## Files to create or change

- `src/Presentation/Panels/RendererTabChrome.cpp` - **new.** Everything the new header declares.
- `src/Presentation/Panels/RendererPanel.cpp` - remove the two toolbar calls from
  `renderStructureWindow`; record the active tab's viewport rectangle; subscribe to
  `CloseRequested`; call `m_TabClose.Drain`, `DrawViewportToolbarOverlays` and
  `DrawRendererTabBarAddButton` from `render`.
- `src/Renderer/Commands/RendererViewportCommands.cpp` - the two new command factories.
- `src/Renderer/Commands/RendererCommandRegistration.cpp` - register `renderer.new_window` and
  `renderer.close_window`.
- `install/users/default/config/keybindings.yaml` - the two bindings.
- `scripts/Windows/GenerateProjects.bat` must be run, because `RendererTabChrome.cpp` is new.

## Files that must NOT be touched

- `src/Presentation/Panels/ViewportToolbars.cpp`, `ViewportVerticalToolbar.cpp`,
  `ViewportTransformToolbar.cpp`, `ViewportToolbars.hpp` - the toolbars themselves are correct and
  move unchanged. If one of them needs an argument it does not have, stop and say so.
- `src/Presentation/Panels/StructureCreationTabsPanel.cpp` - it draws its own three panes and its
  own toolbar pair, is not part of the dockspace tab model, and must keep working exactly as it
  does. The plan names it as the thing most likely to make this bigger than it looks; the answer is
  to leave it alone.
- `src/IO/SceneObjectsIO.hpp`, `src/Presentation/EditorLayer.cpp`'s scene save loop,
  `install/users/default/config/project_windows.txt` handling - the persistence slice is a separate
  task and a format change. Nothing in this task may touch how a window is keyed on disk.
- any header listed under "The contracts, already written" - those are the contract. If you believe
  a signature is wrong, stop and say so instead of changing it.
- anything under `tests/` - a separate session owns the tests.

## The two command ids, their chords and their contexts

| id | chord | context | why |
|----|-------|---------|-----|
| `renderer.new_window` | `Ctrl+T` | `""` | opening an empty tab is harmless from anywhere |
| `renderer.close_window` | `Ctrl+W` | `renderer.viewport.focused` | scoped on purpose: Ctrl+W is a destructive chord and must not fire while the user is typing in the text editor or working in another panel |

`renderer.viewport.focused` is an existing context - the align-axis bindings already use it.

## Acceptance criteria

1. Ctrl+T opens a new empty renderer tab. It appears in the central node's tab bar next to the
   others and is immediately usable.
2. A `+` at the end of the central node's tab bar does the same thing, and appears only when the
   central node actually holds a renderer window.
3. Ctrl+W, with a renderer viewport focused, closes the tab showing. Ctrl+W with focus in another
   panel does nothing.
4. Ctrl+W on a tab with no structure and no scene objects closes it with no prompt, exactly as
   clicking the window's X does.
5. Ctrl+W on a tab holding a structure or any scene object opens a confirmation. Confirming closes
   it; cancelling leaves it open and leaves every other tab alone.
6. Holding Ctrl+W does not stack prompts.
7. Exactly one horizontal toolbar and one vertical toolbar are on screen for the renderer, however
   many renderer tabs are open, and they act on the tab that is showing.
8. Switching tabs changes what the toolbars act on, with no flicker and no frame where they act on
   the previous tab.
9. A renderer window dragged out of the dockspace still gets its toolbars, over its own viewport.
10. The toolbars do not swallow clicks meant for the viewport: a click outside their rectangles
    selects, orbits or drags exactly as it does today.
11. `StructureCreationTabsPanel`'s three-pane window is visually and behaviourally unchanged.
12. No renderer window draws a toolbar inside itself any more - `renderStructureWindow` contains no
    call to either toolbar function.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Presentation` collects intent; the two new actions go
  through `CommandRegistry` and the `EventBus`, never a raw ImGui key check and never a direct call
  into `RendererLayer::RemoveWindow` from a key handler.
- `Renderer` stays exception-free.
- `.cpp` files stay under ~500 lines. `RendererPanel.cpp` is already at 885 and this task must make
  it SMALLER, not larger: the toolbar calls leave it and nothing of substance replaces them. If your
  change grows it, you have put something in the wrong file.
- The overlays are ImGui windows of their own. Draw them after the loop over
  `RendererLayer::GetWindows()`, never inside it.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
