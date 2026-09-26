# Task 41 S11k: undo keeps what you had selected

## Goal

Pressing Ctrl+Z after dragging a style slider deselects the path you were editing, so continuing
to work on it means re-clicking it every time. The same loss happens when the side panel is
resized.

After this task, an undo leaves selected whatever is still there to select, and resizing a panel
does not change the selection at all.

## Defect A - undo clears every selection

`RestoreSceneObjectsSnapshot` (`src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp:112-117`)
clears all six selection vectors unconditionally:

    window.selectedPinnedMeasurements.clear();
    window.selectedFreeLabels.clear();
    window.selectedSceneArrows.clear();
    window.selectedSceneOrbitals.clear();
    window.selectedScenePlanes.clear();
    window.selectedScenePaths.clear();

That is defensible for an undo that removed objects - a selection entry pointing at something that
no longer exists shows the wrong Properties section - but it is wrong as a blanket rule. A style
edit changes no ids at all, and the user loses the selection they were in the middle of editing.

**Fix it in this one shared function**, not at the call sites. It is the restore path for every
scene-object undo, so arrows, orbitals, planes, labels and measurements all gain the same
behaviour from one change.

The rule: after the restore, keep each selection entry whose object still exists in the restored
state, and drop the ones that do not. An undone deletion brings its ids back and they are
selectable again; an undone creation takes its ids away and they leave the selection. The four
vector-backed kinds are index-based, so their entries have to be validated against the restored
vector's size, not just kept.

## Defect B - resizing the side panel deselects

Reported as "the same happens when resizing the side panel (`n`)". Not yet diagnosed. Most likely
the drag that resizes the splitter ends with a mouse release the viewport interprets as a click on
empty space, and empty-space click clears the selection - `ViewportPicking.cpp:33` is the clear,
and the guard that should stop it is knowing the press did not begin inside the viewport.

**Confirm the cause before changing anything.** If it turns out to be something else, report what
it actually is rather than applying the guard above on the strength of this paragraph. If it is
this, the fix is that a click only clears the selection when its press AND its release both
happened inside the viewport.

## Files to create or change

- `src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp`
- whatever Defect B turns out to need, most likely
  `src/Presentation/Panels/ViewportPicking.cpp`

## Files that must NOT be touched

- `src/Renderer/Commands/SceneObjectsSnapshotCommand.hpp` - no signature change is needed
- `src/Renderer/Path/`, `src/IO/` - unrelated
- anything under `tests/` - a separate session owns the tests
- the call sites of `RestoreSceneObjectsSnapshot`. The point of this task is that they do not each
  need to know about selection.

## Acceptance criteria

1. Undoing a style-only edit leaves `selectedScenePaths` exactly as it was.
2. Undoing a deletion restores the deleted objects AND leaves the selection valid - no entry
   pointing at anything that is not there.
3. Undoing a creation removes the created object's id from the selection.
4. The same holds for arrows, orbitals, planes, free labels and pinned measurements, since they
   share the function. Index-based entries are validated against the restored container's size.
5. No selection entry ever survives that would index out of bounds or name a missing id. This is
   what the blanket clear was protecting against and it must stay protected.
6. Defect B: resizing the side panel leaves the selection untouched.
7. A genuine click on empty space inside the viewport still clears the selection.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` stays exception-free.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Report Defect B's actual cause with `file:line` evidence, whether or not it matches the guess
  above.
