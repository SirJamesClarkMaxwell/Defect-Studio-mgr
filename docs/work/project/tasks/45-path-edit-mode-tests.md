# Task 45 tests: the path Edit Mode session

Tests for `docs/work/project/tasks/45-path-edit-mode-session-and-overlay.md`. That task's
implementation is already in the tree; this one writes tests only.

## What is testable and what is not

`PathEditSession` is pure: no ImGui, no GL, no camera. It is the whole of this file's scope, and it
is worth testing because two of its rules are the kind that fail silently — a selection surviving
into the next session shows the wrong thing in the Properties panel, and an id outliving its element
can collide with a later element that reuses the number.

Not testable here, and not to be faked: the overlay drawing, `Tab`/`Esc`/`1`/`2`/`3`, and the click
routing all need a live ImGui frame and a camera. They are verified by hand. Do not build a stub
ImGui context to make them look covered.

`BuildPathHandleMarkers` and `PickPath` already have their own tests and their own contracts. Do not
re-test them through the session.

## Files to create or change

- `tests/Renderer/Path/PathEditSessionTests.cpp` - **new.**
- `scripts/Windows/GenerateProjects.bat` must be run, because the test file is new.

## Files that must NOT be touched

- anything under `src/` - if a test cannot be written because a signature is wrong, STOP and say so.
- any other test file.

## What to assert

Read `src/Renderer/Path/PathEditSession.hpp` first. Every comment in it is a rule, and the cases
below are those rules restated - if the two ever disagree, the header wins and you should say so.

1. A default-constructed session is not active, has an empty selection, and its active element is
   unset.
2. `Enter` makes it active and reports the path it was given.
3. `Leave` makes it inactive AND clears the selection.
4. Entering a different path clears the selection. Entering the SAME path again - write this case
   too, and make it assert whatever the header says; if the header does not say, leave the case out
   and report that gap rather than inventing an answer.
5. `SetSelection` on an inactive session is ignored and leaves the selection empty.
6. `ActiveElement` is the LAST element of the selection, not the first. Use a selection of at least
   three so that first, middle and last are distinguishable.
7. `IsSelected` is true for every element in the selection and false for one that is not in it.
8. `ClearSelection` empties the selection and unsets the active element, without leaving the session.
9. `SetElementMode` / `ElementMode` round-trip for all three values.
10. `PruneSelection` drops ids that are not in the path and KEEPS THE ORDER of those that remain -
    assert the surviving order explicitly, because "the survivors" is not the same claim as "the
    survivors, still in order", and only the second one keeps the active element correct.
11. `PruneSelection` against a path whose id is not the session's leaves the selection untouched.
12. `PruneSelection` on an inactive session does not crash and changes nothing.

For the cases that need a real `ScenePath` with elements, copy how existing tests build one -
`tests/Renderer/Path/` has several, and `tests/Presentation/Panels/RendererTabChromeTests.cpp`
builds one with `AllocateElementId`. Do not invent a different way of constructing a path.

## Constraints

- **You cannot run these tests.** Assert relationships and the header's stated rules, never a value
  you would have to predict. If a case needs a number, an order or an id you have not been told,
  it is a guess - leave it out and say so in your report. A guessed test that happens to pass is
  worse than a missing one, because it reads as coverage.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
