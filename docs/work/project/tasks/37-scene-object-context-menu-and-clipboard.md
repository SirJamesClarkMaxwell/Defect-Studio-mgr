# Task 37: right-click menu + copy/duplicate/paste for every scene object kind

From task 35 items #4 and #6, folded into one task - they turned out to be the same gap seen from
two entry points (outliner row vs. keyboard), and should share one clipboard mechanism per kind
rather than becoming two separate systems.

## What already exists (read this before writing anything)

- Scene arrows already have a working clipboard: `GetSceneArrowClipboard()` /
  `CopySceneArrowsToClipboard` / `PasteSceneArrowsFromClipboard`
  (`Presentation/Panels/SceneArrowEditorWidget.hpp:86-93`), wired to `Ctrl+C`/`Ctrl+V`/`Ctrl+D` in
  `ViewportLabelInteraction.cpp:164-174` - **but only while the mouse hovers the 3D viewport**, and
  only reachable by keyboard, not from any menu. This is very likely why the user reported arrows
  as missing it too (item #6 said "dla strzałek, płaszczyzn" - for arrows *and* planes) - confirm
  with the user whether Ctrl+C/V/D over the viewport with an arrow selected actually works for them
  before assuming the existing mechanism is broken and not just undiscoverable.
- Planes, orbitals and free labels have **no** clipboard at all - grep confirms nothing named like
  `ScenePlaneClipboard`/`SceneOrbitalClipboard` exists.
- The Scene Outliner already has one `BeginPopupContextItem()` context menu
  (`SceneOutlinerPanel.cpp:413-433`), but it is on the top-level window/project row only ("Copy
  view + visibility to..."). Individual object rows (an arrow, a plane, an orbital, a label) inside
  the outliner tree have no context menu at all - that is the actual gap behind item #4.

## Goal

After this task:

1. Planes and orbitals have the same shape of clipboard as arrows (copy selected / paste at cursor
   or with an offset / duplicate-in-place), reusing the arrow clipboard's pattern
   (`GetSceneArrowClipboard`-equivalent, anchor-detaching on paste per
   `PasteOffsetsAndDetachesAnAnchoredArrow`'s existing test) rather than inventing a new shape.
   Free labels too if time allows - lowest priority of the four, they are simpler objects.
2. Every scene-object row in the Scene Outliner (arrow, plane, orbital, label - not the top-level
   window row, which keeps its existing menu untouched) gets a right-click context menu with
   Delete, Duplicate, Copy, and Paste (paste enabled only when the clipboard for that kind is
   non-empty). These menu items call the *same* underlying functions the keyboard shortcuts do -
   no parallel deletion/duplication logic.
3. `Ctrl+C`/`Ctrl+V`/`Ctrl+D` for planes and orbitals work the same way arrows' already do (mouse
   over the viewport, object of that kind selected) - extend the existing block in
   `ViewportLabelInteraction.cpp` rather than writing a second key-handling block elsewhere.

## Files to create or change

- `src/Presentation/Panels/ScenePlaneEditorWidget.{hpp,cpp}` (or wherever `ScenePlane`'s equivalent
  of `SceneArrowEditorWidget` already lives - check before assuming the filename) - add the
  clipboard functions.
- `src/Presentation/Panels/SceneOrbitalEditorWidget.hpp` (check for a matching `.cpp`) - same, for
  orbitals.
- `src/Presentation/Panels/ViewportLabelInteraction.cpp` - extend the existing Ctrl+C/V/D block.
- `src/Presentation/Panels/SceneOutlinerRows.cpp` - the new per-row context menu.
- New tests under `tests/` mirroring the existing
  `SceneArrowClipboardTests.PasteOffsetsAndDetachesAnAnchoredArrow`-style coverage for each kind
  that gets a clipboard.

## Files that must NOT be touched

- `src/Presentation/Panels/SceneArrowEditorWidget.{hpp,cpp}` - the arrow clipboard is the reference
  implementation and already works; do not restructure it, only follow its shape.
- `src/Presentation/Panels/SceneOutlinerPanel.cpp:413-433` - the existing top-level window context
  menu ("Copy view + visibility to...") is unrelated and already correct.
- `src/Renderer/Scene/SceneTransform.{hpp,cpp}`, `src/Renderer/Scene/ScenePlaneGeometry.{hpp,cpp}` -
  unrelated to copy/paste/delete.
- Anything under `src/Domain/`, `src/IO/`, `src/App/`, `src/ScientificRuntime/`.

## Acceptance criteria

1. A GoogleTest per newly-clipboard-capable kind (plane, orbital) mirroring
   `SceneArrowClipboardTests.PasteOffsetsAndDetachesAnAnchoredArrow`: paste offsets position and
   detaches any anchor.
2. A GoogleTest asserts Delete/Duplicate/Copy/Paste menu actions on an outliner row call the same
   functions the keyboard path does (no duplicated deletion logic to drift out of sync).
3. Full Release test suite green: 2 skipped is the `DS_PYTHON_CAPI_AVAILABLE=0` count, not a
   regression.

## Constraints

- Layer boundaries from `AGENTS.md` are hard: user actions go through `CommandRegistry`/existing
  handler patterns, not new ad-hoc key checks outside what already exists in
  `ViewportLabelInteraction.cpp`.
- `.cpp` files stay under ~500 lines - split rather than grow.
- UI strings in this codebase are unaccented Polish. Match the surrounding style.
- Do not build or run anything needing an approval step. Report what you changed, including
  whichever exact file already holds `ScenePlane`'s and the orbital's editor widgets (found by
  reading, not guessed) if it differs from the guesses above; the dispatching session builds, runs
  the suite and exercises the app.
