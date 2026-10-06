# Task 41 S11l: the side panel publishes its rectangle

## Goal

Dragging the N panel's edge to resize it still clears the viewport selection. S11k tried to fix
this and the fix does not hold.

## Why the first attempt failed

`RendererPanel.cpp:332-338` infers the panel's rectangle from `ImGui::GetItemRectMin()` /
`GetItemRectMax()` - the rectangle of whatever item happened to be submitted last at that point -
and then decides by heuristic whether that rectangle "looks like the side panel" (touches the top,
the bottom and the right edge of the image). In the same commit, `DrawViewportSidePanel` was moved
to the end of the draw sequence, so the last submitted item at the click test is no longer the
panel. The guard now protects the wrong rectangle.

`ViewportSidePanel.cpp:85-87` already carried a `ponytail:` comment naming the correct fix:
publish the panel's rect into the pick mask. Do that.

## The contract

`src/Presentation/Panels/ViewportSidePanel.hpp` is already updated and is the contract:
`DrawViewportSidePanel` now returns a `ViewportSidePanelRect` - the screen rectangle it occupied,
grip included, empty when the panel is hidden.

## Files to create or change

- `src/Presentation/Panels/ViewportSidePanel.cpp` - return the rect it drew, grip included. The
  grip is `kGripWidth` wide and sits immediately left of `panelOrigin`, so the published rect
  starts there, not at the panel's own left edge. Delete the `ponytail:` comment that asked for
  this; it is done.
- `src/Presentation/Panels/RendererPanel.cpp` - keep the returned rect, and decide
  `startedOnSidePanel` from it with `Contains(clickPosition)`. Delete the `GetItemRectMin/Max`
  inference and the "looks like the side panel" heuristic entirely.

## Files that must NOT be touched

- `src/Presentation/Panels/ViewportSidePanel.hpp` - written, it is the contract
- `src/Renderer/` - this is entirely a Presentation input-routing bug
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. `DrawViewportSidePanel` returns the rectangle it drew, including the grip, and an empty rect
   when the panel is not visible.
2. `RendererPanel` decides `startedOnSidePanel` from that returned rect and nothing else. No
   `GetItemRectMin`/`GetItemRectMax` and no shape heuristic survive.
3. The guard holds whatever order the panel is drawn in relative to the rest of the viewport
   content. Moving the call must not silently disable it.
4. Pressing on the grip and dragging resizes the panel and leaves the selection untouched.
5. Pressing inside the panel's body leaves the selection untouched.
6. A genuine click on empty space in the viewport still clears the selection.
7. Every existing guarded path keeps its guard: Cursor3D placement, the measure tools and the
   ordinary pick all currently test `startedOnSidePanel` and must continue to.

## Constraints

- Layer boundaries from `AGENTS.md` are hard.
- `.cpp` files stay under ~500 lines.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- The rect is per-frame UI state, not window state: return it, do not add a field to
  `RendererWindowState`. Nothing outside this one frame's input routing needs it.
