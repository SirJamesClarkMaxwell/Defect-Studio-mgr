# Task 43 tests: renderer tab chrome

Tests for `docs/work/project/tasks/43-renderer-tab-chrome.md`. That task's implementation is owned
by a different session; this one writes tests only.

## What is testable and what is not

Most of this feature is ImGui drawing, which has no context in the test binary and is verified by
hand. Three things are pure and belong in tests:

- `ParseRendererWindowId` - a pure string function, and the part of the feature most likely to be
  subtly wrong without anything on screen showing it.
- `RendererTabHoldsContent` - a pure predicate over a `RendererWindowState`. It decides whether
  Ctrl+W asks before discarding, so a false negative here is silent data loss.
- The two command factories - the same shape as
  `tests/Renderer/RendererViewportCommandTests.cpp` already tests for the viewport commands.

`ResolveActiveRendererWindowId`, the overlays, the `+` button and `RendererTabCloseCoordinator::Drain`
all need a live ImGui frame and a dockspace. Do not try to test them, and do not add a fake ImGui
context to make it look possible.

## Files to create or change

- `tests/Presentation/Panels/RendererTabChromeTests.cpp` - **new.**
- `tests/Renderer/RendererViewportCommandTests.cpp` - extend with the two new commands. Match the
  style already in that file.
- `scripts/Windows/GenerateProjects.bat` must be run, because the test file is new.

## Files that must NOT be touched

- anything under `src/` - a separate session owns the implementation. If a test cannot be written
  because a signature is wrong, STOP and say so.
- any other test file.

## What to assert

**`ParseRendererWindowId`** - the name RendererPanel builds is
`"<displayTitle>###RendererWindow_<windowId>"`, and `displayTitle` is the structure's title with a
`"*"` appended when it is dirty.

1. A well-formed name returns exactly the id after `###RendererWindow_`.
2. A dirty window's name - the same with `*` before the `###` - returns the same id.
3. A title that itself contains `###` returns the id after the LAST marker, not the first.
4. A name with no `###` returns an empty string.
5. A name with `###` but some other prefix after it returns an empty string.
6. A name ending exactly at `###RendererWindow_` with no id returns an empty string.
7. An empty input returns an empty string.

**`RendererTabHoldsContent`** - a default-constructed `RendererWindowState` is the empty tab.

1. A default-constructed window holds nothing.
2. A window with a set `structureId` holds content.
3. A window with one `SceneArrow` holds content, and likewise one `SceneOrbital`, one `ScenePlane`,
   one `FreeLabel`, one path in its `PathSystem`.
4. Adding a thing and then removing it again leaves the window holding nothing - the predicate reads
   the current contents, not a flag that latched.

Write case 3 as one case per object kind, not one case that adds all of them. A single combined
case passes even when the predicate only ever looks at the first container.

Do not assert anything about which container the predicate checks first, or in what order. Assert
only the answer.

**The two commands** - follow `RendererViewportCommandTests`'s existing shape: subscribe, execute,
check the event arrived.

1. `CreateRendererNewWindowCommand` publishes `RendererEvents::Windows::OpenEmptyRequested`.
2. `CreateRendererCloseWindowCommand` publishes `RendererEvents::Windows::CloseRequested` with an
   EMPTY `windowId`. The empty id is the contract - the command must not guess which window is
   active - so assert it is empty, not merely that the event arrived.

## Constraints

- **You cannot run these tests.** So assert relationships and contract statements, never a value you
  would have to predict. If a test would need to know a coordinate, a count or an axis you have not
  been told, it is a guess - leave it out and say so in your report.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
