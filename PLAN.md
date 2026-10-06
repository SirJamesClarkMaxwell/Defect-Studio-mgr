# Plan: Arrow/drawing module — selection-registry sync audit, handle hit-test fix

_Locked via grill — by Claude Sonnet 5 + pzabier@gmail.com, 2026-09-19. Round 3: scope cut after
Codex round 2 found item #13 (per-segment Bezier handles) needs its own design pass (picking
arbitration, straight-segment fast path, YAML downgrade safety — 12 findings, see
PLAN-REVIEW-LOG.md). #13 is deferred to a follow-up task, not in this plan._

Branch: `task/39-orbital-orientation-align-and-phase-flip` (or a fresh `task/40-arrow-module-fixes`
off it — implementer's call).

## Goal

Fix two confirmed, code-verified bugs in the scene-arrow module:
- **Bug A**: scene-arrow creation paths allocate a `SceneObjectId` but never register a
  `SceneRegistry` entity for it, so registry-mediated selection resolution silently drops
  newly-created arrows (and likely orbitals/pins/labels via the same pattern) until some unrelated
  action happens to trigger a full resync.
- **Bug B**: two independent, uncoordinated endpoint hit-test systems both grab a single arrow
  endpoint on a click that's merely *near* it, with no matching visible marker: `ViewportGizmo.cpp`'s
  three 5px hint circles hit-tested at a fixed 10px radius, and `ViewportSceneArrowInteraction.cpp`'s
  separate initial click-to-select/drag dispatcher, tolerance `max(14px, shaft-half-width + 8px)`.
  Either one alone explains a plain click silently hijacking to Start/End/midpoint with no visual cue
  why.

Item #13 (interior path-point dragging + per-segment Bezier curve handles) and items #2/#12/#14/
outliner-shortcuts are **out of scope** — see Out of scope.

## Investigation summary (code-verified, not guessed)

**Bug A.** `RendererPanelOrbitalMenu.cpp`'s `AddFreeSegment` (line 97-110) and the `addSegment`
lambda inside `DrawSegmentAddItems` (line 159-178) both do:
```
arrow.id = windowState.sceneRegistry.AllocateObjectId();
windowState.sceneArrows.push_back(std::move(arrow));
```
`AllocateObjectId()` (`SceneRegistry.cpp:34-37`) only increments a counter and returns an id value —
it does **not** call `CreateObject`/insert into `SceneRegistry`'s `m_ObjectEntities` map. No
`SyncLabelEntities` call follows either creation path. Confirmed live today: `renderSceneArrows`'s
`isSelected` check (fed by `RendererLayer.cpp`'s `SceneSystem::ResolveSourceIndices`, which looks up
`SceneRegistry::EntityForObjectId`) was `false` for a selected, gizmo-active arrow across 20,000+
consecutive logged frames — the arrow's gizmo worked throughout (it resolves via a direct
`AnnotationIndex` scan in `SceneTransform.cpp`, not through the registry), but the registry never had
an entity for it. `SceneRegistry`'s own map/lookup implementation (`SceneRegistry.cpp:23-75`) and
`SceneSystem::SyncLabelEntities` (`SceneSystem.cpp:224-305`, which *does* correctly build registry
entities for arrows/orbitals/labels/pins when called) are both verified correct — this is a missing
call at arrow-creation time, not a broken sync mechanism.

**Bug B.** Two separate systems both narrow a click to a single arrow endpoint with no matching
visible marker:
- `ViewportGizmo.cpp:239-260` draws three 5px `AddCircleFilled` hint dots at an arrow's
  Start/End/midpoint (only shown once the arrow is already the sole selection), hit-tested with a
  separately-hardcoded `10.0f` literal (`glm::distance(mouse, *screen) <= 10.0f`).
- `ViewportSceneArrowInteraction.cpp:280-290` — the *initial* click-to-select/drag dispatcher, fires
  on any click that hits an arrow, not just when it's already selected — sets
  `sceneArrowDragTarget`/`sceneArrowGizmoActiveTarget` to Start/End when the click lands within
  `max(14.0f, hitShaftHalfPx + 8.0f)` px of an endpoint (at least 14px, more for a thick shaft), with
  **no drawn marker at all** for this tolerance zone.

Live-repro'd today: three screenshots of the same static arrow (camera orbited between shots, nothing
else changed) each showed the gizmo cross on a different one of Start/End/midpoint; log confirmed
`sceneArrowGizmoActiveTarget` cycling across the session. Either system alone is sufficient to explain
a plain click silently hijacking to a single endpoint; which one fired in the live repro wasn't
isolated (not mutually exclusive — the interaction-dispatcher's click-time decision and the gizmo's
per-frame hint-circle decision can each independently land on the same or different targets).

**Ruled out:** `RendererWindowState::viewOffset` (piped through the renderer as `u_SceneOffset`) is
export-preview-only (confirmed zero outside `ExportImagePanel.cpp`'s dialog-local copy) — not the
cause during normal editing.

## Approach

0. Start from a clean point on `task/39-...` or cut `task/40-arrow-module-fixes` — implementer's call.

1. **Fix A — extract one small, testable helper and route every scene-arrow creation/duplication/
   paste path through it**, instead of patching each UI-local function/lambda individually (those
   aren't unit-testable as-is). Something like:
   ```
   SceneObjectId AppendSceneArrow(RendererWindowState &windowState, RendererWindowState::SceneArrow arrow);
   ```
   `AppendSceneArrow` **unconditionally overwrites `arrow.id`** with a freshly allocated one
   internally (never "if unset", never trusting the caller to have cleared it — a duplicated/pasted
   arrow already carries the *source* object's id, and reusing it would collide with the original in
   `SceneRegistry`'s id→entity map), pushes to `windowState.sceneArrows`, and returns the new id — it
   does **not** sync or touch selection itself (no separate wrapper function for the sync step either
   — call the existing `SceneSystem::SyncLabelEntities` directly, it's already public). Call sites
   collect the returned ids, set `windowState.selectedSceneArrows` to the **complete** batch once all
   appends are done, then call `SceneSystem::SyncLabelEntities` once. Known-confirmed gaps to route
   through this: `AddFreeSegment` and
   `DrawSegmentAddItems`'s `addSegment` (`RendererPanelOrbitalMenu.cpp`); arrow duplicate/paste and
   property-panel creation (confirmed gaps per Codex round 3). Also check the equivalent orbital/pin/
   label creation paths while touching this. **Explicitly excluded**: persistence load and undo/redo
   snapshot restoration (`SceneObjectsSnapshotCommand.cpp:104`) — restoration deliberately preserves
   the original ids and already calls its own sync; routing it through `AppendSceneArrow`'s
   fresh-id contract would break undo (Codex round 5 caught this — round 4's draft wrongly listed
   undo-restore as a routed site). Do **not** touch `ResolveSourceIndices`, `SceneRegistry`, or
   `SyncLabelEntities` itself — all three are correct. Regression tests: unit-test `AppendSceneArrow`
   directly (returned id differs from any input id, `EntityForObjectId` valid only after the paired
   sync call, not before), one test per routed call site (creation, duplicate, paste) confirming the
   resulting selection ids are all distinct and all resolve, and a separate test confirming undo
   restores an object's *original* id unchanged (not routed through the new helper).

2. **Fix B — remove the invisible pre-selection endpoint grab instead of trying to make three
   uncoordinated systems visually consistent.**
   - `ViewportSceneArrowInteraction.cpp:280-290`: the click that *establishes* selection (arrow not
     already the sole selection) always sets `sceneArrowDragTarget`/`sceneArrowGizmoActiveTarget` to
     `Both` — delete the Start/End distance check on that path entirely. There is no visible marker
     before selection, so there must be no sub-handle grab before selection either.
   - Once an arrow is already the **sole** selection, endpoint grabbing happens **only** through the
     markers that are already visibly drawn — `RendererPanel.cpp:273-319`'s orange Start/End dots
     (5px normal / 7px active). A multi-selection (several arrows selected together) keeps today's
     behavior: `RendererPanel` still draws a dot at every selected arrow's endpoints (unchanged), but
     none of them are individually pickable — dragging always moves every selected arrow's `Both`,
     matching the existing `singleArrowOnly` gate already used elsewhere in this code (e.g.
     `ViewportGizmo.cpp:195`). Do not add new multi-selection sub-handle behavior.
   - **Ordering constraint (Codex round 5):** `RunViewportGizmoChain` (which dispatches the hit-test)
     runs *before* `RendererPanel.cpp`'s marker-drawing block this same frame
     (`RendererPanel.cpp:267` vs. `:273-319`) — so hit-testing cannot simply "hit-test against
     RendererPanel's dots" if that means waiting for them to be drawn first. Instead: factor the
     marker geometry (screen position + radius, for Start/End/`Both`) into one small shared function
     that takes the arrow + camera + operation and returns `{point, radius}` per handle; call it from
     the hit-test *and* separately from the draw call, so both read from the same source without
     draw needing to run first. Delete `ViewportGizmo.cpp:239-260`'s own separate hint-circle drawing
     once its hit-test is rebuilt on the shared function. The `Both`/midpoint target has no dot today;
     give it one via the same shared function if it stays pickable post-selection.
   - Manual test: first click on an arrow (not yet selected) anywhere along its length, including
     near an end, always grabs `Both`. A second click, now that it's the sole selection, near a drawn
     endpoint dot grabs that endpoint; a click just outside the dot's visible radius grabs `Both`
     instead. With two arrows selected together, no click grabs a single endpoint on either.

## Key decisions & tradeoffs

- **Fix A is a missing-call audit, not a registry redesign.** `ResolveSourceIndices`/`SceneRegistry`
  are sound (verified by reading their implementation); every other scene-object mutation site in the
  codebase already calls `SyncLabelEntities` correctly. This closes the actual gap instead of working
  around it by switching call sites to a different resolution helper.
- **One shared radius constant for Fix B**, not just a bigger circle — a bigger circle with an
  unchanged, separately-hardcoded hit-test radius would still leave an invisible halo, just a smaller
  one; tying both to the same constant is what actually removes the "clicked near it, not on it"
  failure mode.

## Risks / open questions

- Bug A's audit list above is not exhaustive — grep for every `windowState.sceneArrows.push_back`/
  `windowState.sceneOrbitals.push_back`/etc. across the codebase during implementation and confirm
  each is followed by a sync, don't stop at the two confirmed today.
- The original, more severe task-35 #1 screenshot (gizmo at the coordinate origin) was not
  reproduced today; Bug B explains today's live repro but may not fully explain that older report.
  Re-test #1 against the old screenshots after Fix A + Fix B land, before closing item #1.
- Whether the outliner-shortcuts-still-broken report (commit `f8d9334`) shares Bug A's root cause is
  unconfirmed — outliner dispatch was not instrumented today. Do not assume it's fixed as a side
  effect; re-test live.

## Out of scope

- **#13 (interior path-point dragging + per-segment Bezier curve handles)** — deferred to its own
  design pass. Codex round 2 (see PLAN-REVIEW-LOG.md) found the naive version conflicts with the
  existing whole-arrow `Both` pick target on straight 2-point arrows, would regress rendering/hit-test
  cost by tessellating every straight arrow, needs an explicit straight/curved toggle, and needs a
  real YAML downgrade-safety policy (an old build must not silently discard curve data on re-save).
  None of that is resolved; do not attempt a quick version of this feature as a side effect of Fix A/B.
- #2 (arrow/line selection unreliable), #12 (tip-type change does nothing), #14 (Arrow2D looks bad) —
  not investigated today; each needs its own live-repro session.
- Fixing `SceneSystem`'s erase functions to keep `SceneRegistry` in sync for atoms/bonds (a separate,
  independently-discovered "orphaned rows" bug, unrelated to Bug A above) — separate task.
- Vacancy markers (#8), redefinable default view (#9) — pre-existing backlog items, untouched.
