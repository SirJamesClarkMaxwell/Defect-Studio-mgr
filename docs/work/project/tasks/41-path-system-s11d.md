# Task 41 S11d: paths reach the keyboard shortcuts

## Goal

Reported from the manual round on the S11a+S11b build: with a path selected in the viewport,
**H, Alt+H, Delete, Ctrl+C, Ctrl+V and Ctrl+D all do nothing.**

S11b gave paths the shared *actions* - `SceneObjectEditKind::Path`, the clipboard,
`SetSelectedSceneObjectsVisible`, `ShowAllSceneObjects` - but not the *entry points*. Every
keyboard handler in this codebase carries its own hardcoded list of scene-object kinds, and none
of those lists learned about paths. The actions are already correct and already tested; this task
only connects them.

## The four sites, and what is wrong with each

Diagnosed before dispatch - verify each one yourself, do not take the line numbers on trust.

1. **H (hide selection)** - `RendererLayer::onHideSelectionRequested`, around
   `src/Renderer/RendererLayer.cpp:2338`. The guard around
   `PushPinnedMeasurementUndoSnapshot` + `SetSelectedSceneObjectsVisible` is an inline
   five-way disjunction over the other selection vectors. `selectedScenePaths` is not in it, so
   for a path-only selection the body never runs. `SetSelectedSceneObjectsVisible` itself already
   handles paths - S11b did that - so the function it guards is correct and only the guard is
   wrong.
2. **Alt+H (show all)** - `RendererLayer::onShowAllRequested`, just below. `ShowAllSceneObjects`
   already handles paths, and this one has no such guard. **Check whether it is actually broken
   before changing anything.** The likeliest explanation for the report is that nothing could be
   hidden in the first place because of site 1, which would make Alt+H look dead without being
   dead. If it works, say so in your report and leave it alone.
3. **Delete** - `HandlePinnedMeasurementKeyboardShortcuts` in
   `src/Presentation/Panels/ViewportLabelInteraction.cpp`, around line 148. There is one
   `if (<kind>Selected && hovered && IsKeyPressed(Delete))` block per kind, ending with Plane.
   Paths need theirs.
4. **Ctrl+C / Ctrl+V / Ctrl+D** - the `selectedDrawingKind` ternary chain in the same function,
   around line 158. It resolves Arrow, then Orbital, then Plane, then `std::nullopt`. Paths are
   not a candidate, so with only a path selected `Ctrl+C` and `Ctrl+D` no-op and `Ctrl+V` falls
   through to the arrow clipboard - which is worse than nothing, because it pastes an arrow when
   the user asked for a path.

## Files to change

- `src/Renderer/RendererLayer.cpp` - site 1 only.
- `src/Presentation/Panels/ViewportLabelInteraction.cpp` - sites 3 and 4.
- `tests/Presentation/Panels/ScenePathShortcutsTests.cpp` (new) - see below.

## Files that must NOT be touched

- `src/Renderer/Scene/SceneVisibility.{hpp,cpp}` - already correct, S11b, tested.
- `src/Presentation/Panels/SceneObjectEditActions.{hpp,cpp}` and `ScenePathOperations.{hpp,cpp}` -
  already correct, S11b. If a test here fails against them, that is a real bug: report it, do not
  patch around it.
- Everything S11c is touching: `ScenePathEditorWidget.*`, `SceneOutlinerPanel.*`,
  `SceneOutlinerRows.cpp`, `SceneOutlinerVisibilityColumns.*`, `ViewportRegionSelect.cpp`,
  `SceneTransform.cpp`, `SceneTransformPaths.cpp`, `ScenePathDevMenu.cpp`, and the three test
  files S11c adds. **S11d is committed on top of S11c; if any of those files is dirty when you
  start, stop and say so.**
- `src/Domain/`, `src/IO/`, `src/App/`, `src/ScientificRuntime/`.

## Out of scope

- Registering path commands in `RendererCommandRegistration` and adding `keybindings.yaml`
  entries. That is S12, and the comment at the Ctrl+C/V/D block explains why these shortcuts
  deliberately bypass CoreLayer: `renderer.selection.copy/paste/duplicate` only ever touch atoms
  and there is no fallback chain in `CoreLayer::dispatchKeyChord`. Follow the existing shape, do
  not start the S12 refactor here.
- Making the four hardcoded kind lists into one shared list. It is the obvious cleanup and it is
  not this task: three of the four lists have different membership rules (the Delete chain fires
  per kind and can fire for several at once, the Ctrl chain picks exactly one, the hide guard is
  annotations-only-excluding-atoms). Unifying them needs its own stage and its own tests.

## Acceptance criteria

Behavioural, in `tests/Presentation/Panels/ScenePathShortcutsTests.cpp`. The ImGui handlers
themselves cannot be tested without a context, so test the reachable logic: the guard condition
at site 1 and the kind resolution at sites 3 and 4. If that means extracting a small named
predicate or resolver out of each handler, do it - a named function with a test beside it is the
point, and it is a smaller change than it sounds.

1. The hide guard is true for a path-only selection, and false when nothing at all is selected.
2. It is still true for each of the five kinds that already worked, one at a time.
3. Hiding a path-only selection clears `visible` on exactly the selected paths and pushes one
   undo entry; one undo brings them back.
4. `ShowAllSceneObjects` after that restores them - and state in your report whether Alt+H was
   ever actually broken.
5. The drawing-kind resolver returns `Path` for a path-only selection.
6. It still returns Arrow, Orbital and Plane for those, in the existing precedence order, when
   several kinds are selected at once - the new Path branch must not change which kind wins for
   any selection that already resolved to something.
7. It returns `std::nullopt` when no drawing kind is selected, so the Ctrl+V arrow fallback is
   unchanged for that case.
8. Delete with a path-only selection removes the paths; delete with a path AND an arrow selected
   removes both, matching the existing per-kind behaviour.

Build and suite:

9. `scripts\Windows\GenerateProjects.bat` succeeds after the new test file.
10. Release build of both `.vcxproj` targets is clean.
11. The full Release suite passes with exactly the two known `DS_PYTHON_CAPI_AVAILABLE=0` skips.

## Constraints

- Layer boundaries from `AGENTS.md` are hard.
- `Renderer` is the exception-free zone: no `throw` under `src/Renderer/`.
- `.cpp` files stay under ~500 lines. `ViewportLabelInteraction.cpp` is already long - check it
  before adding, and split rather than go over.
- `near` and `far` are Windows macros. Never use them as identifiers.
- Every store mutation goes through `PathCommands`.

## Manual round

With a path selected in the viewport: H hides it, Alt+H brings it back, Delete removes it,
Ctrl+C then Ctrl+V pastes a copy of the *path* (not an arrow), Ctrl+D duplicates it. Then with a
path and an arrow selected together, confirm Delete removes both and Ctrl+C copies the arrow -
the precedence that already existed must not have changed.
