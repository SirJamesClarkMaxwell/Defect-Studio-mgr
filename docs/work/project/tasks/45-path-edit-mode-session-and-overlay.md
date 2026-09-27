# Task 45: path Edit Mode - the session, the whiskers, and picking an element

First slice of the Edit Mode stage in `docs/work/project/plans/2026-09-20-path-system-implementation.md`.

## Goal

`Tab` opens the selected path for editing. Its nodes appear as markers and every Cubic segment's
Bezier handles appear as whiskers tethered to their node. Clicking one selects it; `1` / `2` / `3`
switch between selecting node-and-handle, segment, and the whole path. `Tab` again, or `Esc`,
closes the session and leaves the object selection exactly as it was.

**Transforms are NOT in this slice.** No G, no R, no S, no dragging a handle, no commands, no undo.
That is the next task, and it is a separate one precisely because it is the half that needs
transactions. If you find yourself writing an undo snapshot, you have gone too far.

## What already exists - this task is mostly wiring

- `src/Renderer/Path/PathHandleGeometry.hpp` - `BuildPathHandleMarkers` already returns one marker
  per node and per Cubic handle, with world and screen positions, draw radii, pick radii, and the
  `owner` id that says which node a handle is tethered to. Its header states it is the single source
  both this overlay and picking read. **Do not compute marker positions anywhere else.**
- `src/Renderer/Path/PathPicking.hpp` - `PickPath` already arbitrates `Handle > Node > Decoration >
  Segment` and already has a `PathPickSettings::editMode` flag whose comment says this stage turns
  it on. **Do not write a second hit-test.**
- `src/Presentation/Panels/SceneObjectMultiSelection.hpp` - `ApplySceneOutlinerSelection` is a
  template over the id type and works on `PathElementId` unchanged. **Do not write Replace / Toggle
  / Range again.**
- The existing Object Mode path selection lives in `RendererWindowState::selectedScenePaths` and is
  not touched by this task.

## The contract, already written

- `src/Renderer/Path/PathEditSession.hpp` - the session type. Read every comment; they are the
  contract, especially the ones about clearing the selection and about `PruneSelection`.

## Files to create or change

- `src/Renderer/Path/PathEditSession.cpp` - **new.** The session.
- `src/Renderer/RendererWindowState.hpp` - one new member, `PathEditSession pathEdit;`, next to
  `selectedScenePaths`. One line plus a comment. This is a central file: keep the diff to that.
- `src/Presentation/Panels/ViewportPathOverlay.cpp` + `.hpp` - **new.** Draws the markers and the
  tether line from each handle to its node.
- `src/Presentation/Panels/ViewportScenePathInteraction.cpp` - `Tab`, `Esc`, `1`/`2`/`3`, and the
  click that selects an element. This file already owns path interaction in the viewport.
- `scripts/Windows/GenerateProjects.bat` must be run - two new `.cpp` files.

## Files that must NOT be touched

- `src/Renderer/Path/PathHandleGeometry.{hpp,cpp}`, `PathPicking.{hpp,cpp}` - they are the sources
  this task consumes. If one of them cannot answer something you need, STOP and say so rather than
  computing it yourself alongside them.
- `src/Renderer/Path/PathCommands.{hpp,cpp}`, anything about undo - not this slice.
- `src/Renderer/Path/PathTopology.*`, `PathEvaluator.*` - no topology changes here.
- `src/Presentation/Panels/ScenePathEditorWidget.*` - the N panel is Object Mode's and is at 491
  lines; do not add to it.
- anything under `tests/` - a separate session owns the tests.

## How it behaves

- `Tab` with exactly one path selected in Object Mode enters Edit Mode on it. `Tab` with no path
  selected, or several, does nothing.
- In Edit Mode the markers are drawn for the edited path only. Other paths keep rendering normally
  and are not pickable.
- A click that hits an element selects it, replacing the selection. Ctrl-click toggles. The new
  selection goes through `ApplySceneOutlinerSelection`.
- A click that hits nothing clears the element selection but stays in Edit Mode. Leaving is `Tab`
  or `Esc`, never an empty click - that would make a missed click destroy the session.
- `1` / `2` / `3` set `PathElementMode`. Changing mode clears the selection, because a segment id
  and a node id do not mean the same thing.
- The active element - `Selection().back()` - is passed as `activeElement` to both
  `BuildPathHandleMarkers` and `PathPickSettings`, so what is drawn enlarged is exactly what is
  easier to click.
- A handle's whisker is a line from the handle marker to the marker of its `owner` node, drawn under
  both markers.

## Acceptance criteria

1. `Tab` with one path selected shows its node markers and its Cubic handles; `Tab` again hides them
   and leaves the Object Mode selection unchanged.
2. `Esc` leaves Edit Mode the same way.
3. Every Cubic segment shows two whiskers, each a line to the node it belongs to. Line and Arc
   segments show none.
4. Clicking a node selects it; clicking a whisker selects the handle, even where the two overlap -
   the arbitration order already guarantees this, so this criterion is about not defeating it.
5. Ctrl-click adds to and removes from the selection; the last one clicked is the active element and
   is drawn enlarged.
6. Clicking empty space clears the element selection and stays in Edit Mode.
7. `1` / `2` / `3` switch mode and clear the selection.
8. A path hidden with `visible == false` shows no markers and picks nothing, in Edit Mode too.
9. Entering Edit Mode on a different path clears the previous element selection.
10. Nothing in Object Mode changes: the N panel, the gizmo and whole-path selection behave exactly
    as before when Edit Mode is off.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `PathEditSession` is `Renderer` and must not include
  anything from `Presentation`. `Renderer` stays exception-free.
- `.cpp` files stay under ~500 lines.
- The design above is APPROVED. Do not stop to ask for confirmation of it - implement it.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
