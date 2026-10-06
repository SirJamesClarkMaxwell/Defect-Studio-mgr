# Task 42: VSCode-style panel layout toggles

## Goal

Three keyboard shortcuts and three menu-bar icon buttons hide and restore whole dock regions the way
VSCode's sidebar toggles do: `Ctrl+B` the left region, `Ctrl+J` the bottom region, `Ctrl+Alt+I` the
right region. A region is whatever is docked there *right now*, computed from the live ImGui dock
node rectangles, not a hardcoded panel list - dragging a panel to the other edge moves it to the
other toggle. Restoring shows exactly the panels that were visible when the region was hidden, so a
panel the user had already closed stays closed. Separately, the Project Tree toolbar gets a `+` that
opens an empty renderer window, the same action the menu bar's `+` already performs.

## Contract (already written - do not change)

- `src/Presentation/PanelDockRegions.hpp` - `DockRegion`, `DockRectangle`, `ClassifyDockRegion`,
  `DockRegionToggle`. Signatures and the documented semantics are fixed.
- `tests/Presentation/PanelDockRegionsTests.cpp` - 9 cases, 6 currently failing against the stubs in
  `PanelDockRegions.cpp`. Expectations are fixed.

Implement `src/Presentation/PanelDockRegions.cpp` (replacing the stubs) and wire it up.

## Files to create or change

- `src/Presentation/PanelDockRegions.cpp` - replace both stubs with the real implementation.
- `src/Presentation/EditorLayerDockRegions.cpp` (NEW) - the EditorLayer half: classify every visible
  panel into a region each frame, register the three commands, apply a `Decision` by calling
  `IPanel::SetVisible`. Keep it out of `EditorLayer.cpp`, which is already 1918 lines against the
  ~500-line rule; `EditorLayerMenus.cpp` is the precedent for splitting EditorLayer methods across
  files.
- `src/Presentation/EditorLayer.hpp` - the new method declarations and the three `DockRegionToggle`
  members plus the per-region title lists.
- `src/Presentation/EditorLayerMenus.cpp` - three icon buttons on the main menu bar, next to the
  existing `+`.
- `install/users/default/config/keybindings.yaml` - three bindings.
- `src/Presentation/Panels/ProjectTreePanel.cpp` / `.hpp` - the `+` toolbar icon.
- `src/Events/RendererEvents.hpp`, `src/App/RendererRuntimeOpenCoordinator.cpp` / `.hpp` - the
  `OpenEmptyRequested` event and its handler (see "Empty window from the Project Tree" below).

## Files that must NOT be touched

- `src/Presentation/PanelDockRegions.hpp` and `tests/Presentation/PanelDockRegionsTests.cpp` - the
  contract. If you believe a signature or an expectation is wrong, stop and say so.
- Anything under `src/Domain/`, `src/IO/`, `src/ScientificRuntime/`.
- `src/Renderer/` except nothing - do not change the renderer at all;
  `OpenEmptyRendererWindow` already exists in `src/Renderer/OpenCrystalStructureAsWindow.hpp` and is
  called as-is.
- `src/Presentation/ImGuiLayer.cpp` - the dockspace is created there and stays as it is; read the
  dockspace id, do not restructure the layout.
- Any other panel's `Render()`.

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=PanelDockRegionsTests.*` - 9/9 pass, with
   `PanelDockRegions.hpp` and the test file byte-identical to what you were given.
2. Full Release suite still at 667 passed / 2 skipped. The two skips
   (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
   `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`) are expected, not
   regressions.
3. `Ctrl+B` hides every panel docked left of the viewport; pressing it again brings back exactly
   those panels. Same for `Ctrl+J` (bottom) and `Ctrl+Alt+I` (right).
4. A panel dragged from the left edge to the right edge is afterwards toggled by `Ctrl+Alt+I`, not
   by `Ctrl+B`. This is the point of the whole task - a hardcoded panel-to-region table fails it.
5. Three icon buttons on the main menu bar do the same three things, and each one's glyph reflects
   whether its region is currently hidden.
6. The Project Tree toolbar has a `+` that opens an empty renderer window.
7. No `keymap.register.conflict` warnings in the log at startup.

## Constraints

- **Layer boundaries** (`AGENTS.md`, hard): `Presentation` renders UI and collects user intent; it
  does not silently mutate other modules' state. Panel visibility is Presentation's own state, so
  `SetVisible` from EditorLayer is fine. Opening a renderer window is not - route it through
  `EventBus`, see below.
- **Empty window from the Project Tree**: `ProjectTreePanel` reaches renderer functionality through
  `EventBus` today (`Open Defect` publishes `RendererEvents::Windows::OpenStructureRequested`,
  `ProjectTreePanel.cpp:255-263`, bridged by `App/RendererRuntimeOpenCoordinator`). Follow that:
  add `RendererEvents::Windows::OpenEmptyRequested`, handle it in `RendererRuntimeOpenCoordinator`
  by calling `OpenEmptyRendererWindow`, and publish it from the Project Tree toolbar. Then change
  the main menu bar's existing `+` (`EditorLayerMenus.cpp`, `renderNewSceneWindowButton`) to publish
  the same event instead of calling `RendererLayer` directly, and drop the now-unneeded
  `Renderer/RendererLayer.hpp` and `Renderer/OpenCrystalStructureAsWindow.hpp` includes from that
  file. One route, and Presentation stops holding a renderer handle for this.
- **Icons**: use Font Awesome glyphs, already available via `IconsFontAwesome6.h` and used by
  `ProjectTreePanel.cpp:737-742` and `LoggingPanel.cpp:149`. Do NOT use
  `RendererLayer::GetToolbarIcon` - that would add OpenGL texture coupling to the editor menu bar
  for three buttons. Suggested: `ICON_FA_ANGLES_LEFT` / `ICON_FA_ANGLES_DOWN` /
  `ICON_FA_ANGLES_RIGHT` when the region is shown, flipped to `ICON_FA_ANGLES_RIGHT` /
  `ICON_FA_ANGLES_UP` / `ICON_FA_ANGLES_LEFT` when hidden (the arrow points at where the region
  will come back from). All four glyphs exist in the vendored header.
- **Dock node access** needs `imgui_internal.h`: `ImGui::DockBuilderGetCentralNode(dockspaceId)` for
  the central rectangle (`RendererPanel.cpp:149-158` is the existing precedent), and
  `ImGui::FindWindowByName(title)` -> `window->DockNode` -> `Pos`/`Size` for each panel. Classify
  only panels that are visible and actually docked; a floating or missing window is `Floating` and
  belongs to no toggle. `ImGuiLayer.cpp:290-302` creates the dockspace - read its id, do not
  recreate it.
- **Shortcut registration**: commands go through `CommandRegistry::Register`
  (`src/Core/Commands/CommandRegistry.cpp:44-81`); `editor.focus_project_tree`
  (`EditorLayer.cpp:594-608` + `keybindings.yaml:23-27`) is the copy-paste template. A duplicate
  chord/context/layer fails registration with a `keymap.register.conflict` warning
  (`KeymapResolver.cpp:60-78`). `Ctrl+B`, `Ctrl+J` and `Ctrl+Alt+I` are all currently free; the bare
  `B` at `keybindings.yaml:247` is a different chord in the `renderer.viewport.focused` context and
  must keep working.
- `.cpp` files stay under ~500 lines.
- Regenerate projects with `scripts/Windows/GenerateProjects.bat` after adding
  `EditorLayerDockRegions.cpp` - premake globs sources at generation time.

## Recon brief (Codex read-only pass, 2026-09-25)

1. PANEL MODEL. `IPanel` lives at `src/Presentation/Panels/IPanel.hpp:11-58`. Visibility is plain
   `m_Visible`, via `IsVisible()`/`SetVisible()` (`:31-39`); title is `GetTitle()` (`:21-24`),
   category is virtual `GetCategory()` (`:44-47`). There is no panel-id accessor: IDs live in
   `PanelRegistry::Entry` (`PanelRegistry.hpp:15-19`). `EditorLayer` owns `PanelRegistry m_Panels`
   (`EditorLayer.hpp:260`) and iterates every entry each frame (`EditorLayer.cpp:569-579`); each
   panel's `Render()` gates itself.
2. VISIBILITY PERSISTENCE. `EditorLayer::savePanelVisibilityState()` writes title=`1/0` to
   `install/users/default/config/panel_visibility.txt` (`EditorLayer.cpp:1009-1022`), called during
   `OnDetach()` (`:535-541`). `applyPanelVisibilityState()` reads it and calls `SetVisible()` after
   all panels register (`:1024-1050`, `:768-771`). Programmatic `SetVisible()` does not immediately
   write; it round-trips only if shutdown follows.
3. DOCK LAYOUT. The global dockspace is `ImGui::DockSpaceOverViewport()` in
   `src/Presentation/ImGuiLayer.cpp:290-302`. Renderer windows only query/dock to the central node
   on first use (`RendererPanel.cpp:149-158`). No code records left/right/bottom placement, and
   there are no actual `DockBuilder*` layout-construction calls - only `DockBuilderGetCentralNode`.
   Placement is ImGui's saved layout (`install/users/default/layouts/imgui.ini`), not panel
   metadata.
4. PANEL CATEGORY. Enum values are `Scene`, `Structure`, `Analysis`, `Project`, `Console`, `Other`
   (`MenuBarModel.hpp:15-25`). Semantic/menu grouping, not positional.
5. COMMANDS + KEYMAP. Commands register through `CommandRegistry::Register(CommandMeta, factory)`
   (`src/Core/Commands/CommandRegistry.cpp:44-81`); bindings call
   `KeymapResolver::RegisterBinding()` (`CoreLayer.cpp:421-428`). Startup loads
   `ConfigManager::GetKeybindingsPath()` (`ApplicationBootstrap.cpp:1004-1009`); default file is
   `install/users/default/config/keybindings.yaml`. Same chord/context/layer causes
   `keymap.register.conflict`, warning logs, failed registration, and a `GetConflicts()` entry
   (`KeymapResolver.cpp:60-78`).
6. MENU BAR ICONS. `RendererLayer::GetToolbarIcon()` is public (`RendererLayer.hpp:129`, impl
   `RendererLayer.cpp:448-451`), loading `<assetsDirectory>/icons/<filename>` with stb_image
   (`RendererLayer.cpp:2482-2509`). Cheapest fallback is Font Awesome text glyphs (`ICON_FA_*`),
   already used by `ProjectTreePanel.cpp:737-742`.
7. PROJECT TREE. `Render()` calls `renderToolbar()` before tree contents
   (`ProjectTreePanel.cpp:189-215`); the toolbar is at `:702-770` with existing quick-action icons
   at `:737-742`. The empty-scene action attaches beside `Create Defect` there and needs no selected
   path. Renderer interaction is EventBus-based (`:255-263`); there is no command ID for it.
8. RISKS. (1) Dock regions are not modeled. (2) Visibility persistence is title-keyed and
   shutdown-only, so renamed/duplicated panels and abrupt exits can lose or misapply state.
   (3) Shortcut conflicts are runtime registration failures; menu icons via the renderer loader
   would add OpenGL coupling to editor UI.
