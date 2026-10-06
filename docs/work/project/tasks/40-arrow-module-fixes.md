# Task 40: arrow module — registry-sync audit (Fix A) + handle hit-test fix (Fix B)

Full design/investigation background: `PLAN.md` (repo root) + `PLAN-REVIEW-LOG.md` (5 rounds of
Codex adversarial review, already applied to PLAN.md). Read `PLAN.md` in full before starting —
this file is the bounded execution slice, PLAN.md has the reasoning and the acceptance criteria
this task's tests were derived from.

## Goal

**Fix A**: every scene-arrow creation/duplication/paste path registers a `SceneRegistry` entity for
the new arrow, so registry-mediated selection resolution (`SceneSystem::ResolveSourceIndices`, used
by `RendererLayer.cpp` for render highlighting) never silently drops a freshly created/duplicated/
pasted arrow. The contract (`SceneSystem::AppendSceneArrow`) and its failing tests are already
written — see "Files already written" below. Implement `AppendSceneArrow` for real, then route every
confirmed and discovered creation/duplicate/paste path through it.

**Fix B**: a plain click that merely lands *near* an arrow's Start/End/midpoint no longer silently
narrows the gizmo/drag target to that single point unless a visible marker was actually there to
click on. The first click that selects a previously-unselected arrow always grabs the whole arrow
(`Both`); only a subsequent click, once the arrow is the sole selection, can grab a single endpoint,
and only by landing on the endpoint dot that's already drawn (`RendererPanel.cpp`'s orange Start/End
markers).

## Files already written (the contract — do not change signatures or test expectations)

- `src/Renderer/Scene/SceneSystem.hpp` — `AppendSceneArrow` declaration (with its full contract in
  the comment above it).
- `src/Renderer/Scene/SceneSystem.cpp` — a deliberately-wrong stub implementation of
  `AppendSceneArrow` (reuses whatever id is already on the incoming arrow instead of allocating a
  fresh one). Replace the stub body; do not change the signature.
- `tests/Renderer/Scene/SceneObjectModelTests.cpp` — three new `TEST(SceneObjectModelTests, ...)`
  cases at the end of the file (`AppendSceneArrowAlwaysAssignsAFreshId`,
  `AppendSceneArrowDoesNotSyncByItself`, `BatchAppendThenSingleSyncResolvesEveryNewArrow`). All three
  currently fail against the stub (confirmed: they compile and fail on assertion, not a link error).
  If you believe one of these three tests is wrong, stop and say so instead of changing it.

## Files to create or change

- `src/Renderer/Scene/SceneSystem.cpp` — real `AppendSceneArrow` body.
- `src/Presentation/Panels/RendererPanelOrbitalMenu.cpp` — route `AddFreeSegment` and
  `DrawSegmentAddItems`'s `addSegment` lambda through `AppendSceneArrow` + one
  `SceneSystem::SyncLabelEntities` call (currently: `AllocateObjectId()` + `push_back`, no sync at
  all — this is Bug A's confirmed, verified root cause).
- Arrow duplicate/paste call sites (grep `windowState.sceneArrows.push_back` and any clipboard/
  duplicate command for scene arrows — likely `SceneArrowOperations.cpp`,
  `SceneObjectEditActions.cpp`, or wherever `SceneArrowClipboard`/`PasteSceneArrowsFromClipboard`
  lives) — route each through `AppendSceneArrow`, batching the sync (one `SyncLabelEntities` call per
  paste/duplicate operation, not one per object).
- Property-panel arrow creation (confirmed gap per Codex review, exact file not yet located — grep
  for another `windowState.sceneArrows.push_back` outside the two already-known sites).
- `src/Presentation/Panels/ViewportSceneArrowInteraction.cpp` (~line 280-290): the click that
  *establishes* selection (arrow not already the sole selection) must always set
  `sceneArrowDragTarget`/`sceneArrowGizmoActiveTarget` to `Both` — delete the Start/End distance
  check on that path. Only keep endpoint-distance logic for the case where the arrow is *already*
  the sole selection.
- `src/Presentation/Panels/ViewportGizmo.cpp` (~line 239-260): delete this file's own separate
  Start/End/midpoint hint-circle drawing and hit-test. Replace with a hit-test against the SAME
  marker geometry `RendererPanel.cpp` draws (see next item) — factor that geometry into one small
  shared function (arrow + camera + operation → `{point, radius}` per handle) callable from both the
  (earlier-in-the-frame) hit-test and the (later) draw call, since `RunViewportGizmoChain` runs
  before `RendererPanel.cpp`'s marker-drawing block this same frame and so cannot depend on that
  frame's already-drawn output.
- `src/Presentation/Panels/RendererPanel.cpp` (~line 273-320): if a `Both`/midpoint marker doesn't
  exist yet and needs to stay pickable post-selection, add it via the same shared geometry function.
  Keep multi-selection behavior unchanged: every selected arrow still gets its endpoint dots drawn,
  but sub-handle picking stays gated to a single-arrow selection (`singleArrowOnly`, matching the
  existing gate in `ViewportGizmo.cpp`).

## Files that must NOT be touched

- `src/Renderer/Scene/SceneRegistry.cpp`/`.hpp`, `SceneSystem::ResolveSourceIndices`,
  `SceneSystem::SyncLabelEntities` (aside from calling it, not changing it) — all verified correct.
- `src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp` (undo/redo restoration) — must keep
  preserving original ids and calling its own sync directly; must NOT be routed through
  `AppendSceneArrow`'s fresh-id contract.
- Anything under item #13 in `PLAN.md`'s "Out of scope" (interior path-point dragging, per-segment
  Bezier curve handles) — not this task.
- `SceneArrowGeometry`, `BuildSceneArrowMesh`, `OpenGlRendererBackend.cpp`'s render path — Fix A/B
  don't touch geometry or rendering, only selection/registry-sync and pick/hit-test logic.
- Everything else in `PLAN.md`'s "Out of scope" section (items #2, #12, #14, outliner shortcuts,
  `SceneSystem`'s erase-function sync bug for atoms/bonds, vacancy markers, default view).

## Acceptance criteria

1. `tests/Renderer/Scene/SceneObjectModelTests.cpp`'s three new tests pass.
2. Every existing test in `SceneObjectModelTests.cpp` still passes (the AppendSceneArrow
   implementation must not break the existing id/resync contract tests above it in the same file).
3. A new scene arrow created via the "Add" menu (Shift+A or the panel menu), immediately selected,
   resolves through `SceneSystem::ResolveSourceIndices` without any other action happening first —
   add a test mirroring `BatchAppendThenSingleSyncResolvesEveryNewArrow` but going through whichever
   real call site you fixed in `RendererPanelOrbitalMenu.cpp`, if that's feasible without dragging in
   ImGui; otherwise note in your report why it isn't and rely on the manual test instead.
4. Duplicating an existing scene arrow produces a new arrow with a different id from the source, and
   both resolve via `ResolveSourceIndices` afterward — test if there's a reachable, non-UI entry
   point for the duplicate command; otherwise note it for the manual pass.
5. `full-build-verify`-equivalent compiles clean in Release (Claude will run the actual skill after
   you're done, this is a sanity bar for you: at minimum `scripts/Windows/Build.bat --config Release
   --target DefectStudioTests` succeeds and the whole suite is green, not just the new tests).
6. Manual/report-only (Claude verifies by hand): first click on an unselected arrow, anywhere along
   its length including near an end, grabs the whole arrow; a second click on an already-selected
   arrow's visible endpoint dot grabs that endpoint; a click just outside the dot's visible radius
   grabs the whole arrow instead.

## Constraints

- Read `AGENTS.md` and `CLAUDE.md` first — layer boundaries there are hard
  (`Domain`/`Renderer`/`IO`/`Presentation`/`App`; this task stays inside `Renderer/Scene` and
  `Presentation/Panels`, touching nothing in `Domain` or `IO`).
- Do not add exceptions to any rendering path.
- `.cpp` files stay under ~500 lines — if a file you're editing is already near that, factor out
  rather than growing it further.
- Regenerate projects with `scripts/Windows/GenerateProjects.bat` only if you add a new `.cpp`/`.hpp`
  file (you likely won't need to for this task — everything above is edits to existing files).
- Do not build or run the full test suite yourself if your sandbox can't do it reliably — Claude
  builds and runs `full-build-verify` (Debug + Release, both `DefectStudio.exe` and
  `DefectStudioTests.exe`) after you report back. Do compile-check what you can locally if your
  sandbox supports it; say so either way in your report.
- Do not touch `SyncScenePendingAppends` — that function does not exist; call
  `SceneSystem::SyncLabelEntities` directly after a batch of `AppendSceneArrow` calls (this was
  simplified away from an earlier draft of the plan, PLAN.md's Fix A step already reflects this).
