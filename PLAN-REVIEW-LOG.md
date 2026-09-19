# Plan Review Log: Arrow/drawing module fixes (selection-registry sync, handle hit-test)
Act 1 (grill) complete — plan locked with the user. MAX_ROUNDS=5.
Codex thread: `01a0b9ac-e46e-7af1-a42d-ec7983e11870` (codex-cli 0.152.1, model per `~/.codex/config.toml`: `gpt-5.6-sol`, reasoning=high).

## Round 1 — Codex: REVISE (15 findings)
Reviewed the original, larger plan (Fix A as a `ResolveSourceIndices`→`AnnotationIndex` swap in
`RendererLayer.cpp`, plus a full per-segment Bezier-handle feature for #13). Key findings:
1-2. **Fix A's root-cause claim was wrong** — "arrows have no SceneRegistry entities" was a false
   negative from a bad grep (`SceneObjectKind::Arrow` doesn't match the real
   `SceneObjectKind::SceneArrow`). `SyncLabelEntities` does register them correctly; an existing test
   proves it. Claude verified this directly in `SceneSystem.cpp`/`SceneRegistry.cpp` and accepted:
   **wrong**, corrected in round 2.
3. Fix A's proposed regression test wouldn't reproduce the real failure. Accepted.
4. Fix B (bigger handle markers) doesn't remove hijacking if the pick radius stays separately
   defined/enlarged. Accepted — Fix B redefined as visibility-only, radius unchanged (later further
   revised in round 5).
5-15. Extensive gaps in the #13 Bezier-handle feature (target identity, anchor determinism, optional
   vs. always-present handles, migration safety, YAML versioning, Arrow2D exclusion, cost bounds,
   test matrix, and more). Claude addressed most in round 2's revision.

## Round 2 — Codex: REVISE (12 findings)
Reviewed the round-1 revision (corrected Fix A narrative, closed most #13 gaps). Key findings:
1. **Confirmed live in code**: `AddFreeSegment`/`addSegment` (`RendererPanelOrbitalMenu.cpp`) never
   call `SyncLabelEntities` — they only call `AllocateObjectId()` (which just reserves an id, doesn't
   create a registry entity). This is the real, verified Bug A root cause. Claude verified directly
   by reading the two functions: **correct**, accepted.
3. Interior-point "dots" Claude assumed already existed (seen in screenshots) don't — verified:
   `RendererPanel.cpp:273-319` draws only Start/End, not interior points. **Correct**, accepted;
   Claude's assumption was based on visual interpretation of screenshots, not a code check.
5-12. #13's per-segment handle design has further problems: collides with the whole-arrow `Both`
   pick target on straight arrows, would regress render/hit-test cost by tessellating every straight
   segment, needs an explicit straight/curved toggle, YAML downgrade safety, truncation-vs-reject
   policy on overlong point lists. Given the growing scope, Claude asked the user how to proceed;
   user chose to cut #13 out of this plan entirely rather than keep iterating on it.

## Round 3 — Codex: REVISE (2 findings + 1 correction), on the scope-cut plan (#13 removed)
1. **Fix B only covered `ViewportGizmo.cpp`'s hint circles** — missed a second, independent,
   larger (≥14px), completely unmarked endpoint-grab tolerance in
   `ViewportSceneArrowInteraction.cpp:280-290`, which fires on the *first* click (before the arrow is
   even selected). Claude verified this directly in code: **correct**, likely the bigger contributor
   to today's live repro. Accepted, Fix B scope expanded.
2. Fix A's per-path tests aren't executable as written (`AddFreeSegment`/`addSegment` are UI-local,
   no test seam). Accepted — Fix A redefined around one small, directly-testable helper function.
Correction: duplicate/paste and property-panel creation also don't sync (not just the two
`RendererPanelOrbitalMenu.cpp` sites) — folded into Fix A's known-gap list.

## Round 4 — Codex: REVISE (3 findings)
1. The append-helper's "allocate id if unset" contract is unsafe for duplicate/paste (source arrows
   already carry a valid, colliding id). Accepted — helper redefined to unconditionally assign fresh.
2. The endpoint pick-zone is invisible *before* selection regardless of which radius is used, since
   no marker is drawn pre-selection. Accepted, and adopted the stronger simplification: first click on
   an unselected arrow always grabs `Both`, no sub-handle before selection at all.
3. `RendererPanel.cpp`'s own endpoint dots (5px/7px) were left outside the "shared radius" contract.
   Accepted — folded into the same unification as point 2.

## Round 5 — Codex: REVISE (4 findings) — MAX_ROUNDS reached
1. Round 4's draft wrongly listed undo/redo snapshot restoration as a site needing the fresh-id
   helper; restoration deliberately preserves original ids and already syncs
   (`SceneObjectsSnapshotCommand.cpp:104`). **Correct** — Claude's error, restoration explicitly
   excluded from the helper's scope in the final plan.
2. The helper contract still left an escape hatch ("assert caller cleared the id") instead of
   unconditionally overwriting internally. Accepted, tightened.
3. Folding `ViewportGizmo`'s hit-test into "test against RendererPanel's drawn dots" doesn't work as
   worded — `RunViewportGizmoChain` (the hit-test) runs *before* `RendererPanel`'s marker-drawing
   block in the same frame, so the hit-test can't depend on that frame's draw call. Accepted — plan
   now specifies a shared geometry-computing function called independently by both the (earlier)
   hit-test and the (later) draw call, instead of literally testing against draw output.
4. Multi-selection endpoint-dot display vs. pickability was left ambiguous. Resolved by decree:
   dots still draw for every selected arrow (unchanged visual), but sub-handle picking stays gated to
   `singleArrowOnly` (matching the existing gate already used elsewhere), same as today.

**Resolution: round 5 (the cap) is not a re-verified APPROVED.** All four round-5 findings were
folded into the plan directly (Claude agrees with all four — this is a round-count cap on Codex's own
review cycle, not a substantive disagreement being overridden). The plan was not re-submitted for a
round-6 check. Flagging this transparently rather than claiming a false APPROVED, per the skill's
"do not fake convergence" rule — the user should treat the current PLAN.md as Codex-hardened through
round 4 and Claude-self-reviewed for round 5's points, not Codex-blessed on the exact final wording.
