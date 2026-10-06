# Task 44: the menu bar's dock toggles get real icons and move right

Small UI task, one file. No new capability - this is the menu bar's right-hand end being made
legible.

## Goal

The three dock-region toggles sit at the right edge of the main menu bar and each one shows, at a
glance, which region it toggles and whether that region is showing. The `+` that opened an empty
renderer window is gone from the menu bar.

## Why the `+` goes

It was the only way to open an empty renderer window from the chrome. Task 43 gave that action
three better homes: `Ctrl+T`, the `+` at the end of the renderer tab bar, and the project tree. A
fourth copy sitting between "Pomoc" and the dock toggles is clutter in the one row the user reads
most often. The action itself is untouched - `RendererEvents::Windows::OpenEmptyRequested` and its
handler stay exactly as they are; only this button disappears.

## The defect in the icons today

`renderMainMenuBar` in `src/Presentation/EditorLayerMenus.cpp` draws:

```
Left   -> ICON_FA_ANGLES_LEFT  when showing, ICON_FA_ANGLES_RIGHT when hidden
Bottom -> ICON_FA_ANGLES_DOWN  when showing, ICON_FA_ANGLES_UP    when hidden
Right  -> ICON_FA_ANGLES_RIGHT when showing, ICON_FA_ANGLES_LEFT  when hidden
```

A hidden Left and a showing Right are therefore the SAME glyph, and a hidden Right and a showing
Left are the same glyph as each other. Two buttons, four states, two symbols - the row cannot be
read. That is the actual bug, not merely that the arrows are ugly.

## Draw the icons, do not pick a font glyph

FontAwesome 6 free has no panel-left / panel-bottom / panel-right icon. Every candidate
(`TABLE_COLUMNS`, `WINDOW_MAXIMIZE`, `BORDER_ALL`) is either identical for Left and Right or means
something else. Adding PNG assets for three icons is more moving parts than the icons are worth.

So draw them with `ImGui::GetWindowDrawList()`, the way VS Code's own panel toggles look:

- an outlined rounded rectangle, the full icon square inset by ~1px - this is the window;
- a bar along one edge of that rectangle - left edge, bottom edge or right edge, matching the
  region - about 35% of the square's extent on that axis;
- the bar is FILLED when the region is showing and left empty (outline only) when it is hidden.

That is one shape with three positions and two states, and all six results are distinguishable.

Colours come from the current style, never hardcoded: `ImGuiCol_Text` for the strokes and the fill,
dimmed via `ImGuiCol_TextDisabled` for an empty bar. The icon square's extent is
`ImGui::GetTextLineHeight()` so it scales with the font like everything else on the bar.

## Right-alignment

The three buttons move to the right edge of the menu bar, after the menus. Compute their total
width - three buttons of the icon extent plus frame padding, plus the item spacing between them -
and set the cursor to `ImGui::GetWindowWidth() - total - ImGui::GetStyle().WindowPadding.x` before
the first one.

Do not hardcode the width. If the font scale changes, the row must stay glued to the right edge.

## Files to create or change

- `src/Presentation/EditorLayerMenus.cpp` - the drawing, the right-alignment, and deleting
  `renderNewSceneWindowButton` and its call.
- `src/Presentation/EditorLayer.hpp` - remove the `renderNewSceneWindowButton` declaration.

## Files that must NOT be touched

- `src/Presentation/EditorLayerDockRegions.cpp` - the toggle behaviour, the commands and the
  Ctrl+B / Ctrl+J / Ctrl+Alt+I bindings are correct and are not part of this. Only how the buttons
  LOOK and where they sit changes.
- `src/Events/RendererEvents.hpp` and anything that publishes or handles `OpenEmptyRequested` - the
  action survives, only the menu-bar button goes.
- `src/Presentation/Panels/RendererTabChrome.cpp` - task 43's `+` on the tab bar stays.
- anything under `tests/`.

## Acceptance criteria

1. No `+` on the main menu bar. `Ctrl+T`, the tab bar's `+` and the project tree still open an empty
   renderer window.
2. The three toggles sit flush against the right edge of the menu bar, with the menus on the left.
3. Each icon shows a window outline with a bar on the left, the bottom or the right, matching the
   region it toggles.
4. A showing region's bar is filled; a hidden region's bar is an empty outline. No two of the six
   states look alike.
5. Clicking a button still toggles exactly the region it did before, and the tooltips still name it.
6. Changing the UI font scale keeps the row glued to the right edge and the icons proportional.

## Constraints

- Layer boundaries from `AGENTS.md` are hard.
- `.cpp` files stay under ~500 lines. `EditorLayerMenus.cpp` is at 290.
- No new image assets, no changes to the font atlas.
- The design above is APPROVED. Do not stop to ask for confirmation of it - implement it.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
