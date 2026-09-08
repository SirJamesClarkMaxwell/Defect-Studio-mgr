# Plan Review Log: Structure Lifecycle Refactor — Steps 10-11 Redesign

Act 1 (grill) complete — plan locked with the user. MAX_ROUNDS=5.

---

## Act 1 Summary

Seven grilling questions established:

1. **4 creation modes:** Template (with atom swaps), Scratch (Bravais + atomic DB), Analyze Existing (load & recognize), Import File. All converge on common supercell generation (hxkxl) and "Add to Project".

2. **Renderer layout:** One tab with 3 resizable renderer windows, shared vertical + horizontal toolbar, only during structure-creation workflow (NewStructure + StructureHub sessions).

3. **Mode location:** "Create from Template" (+ other modes) in NewStructure panel, NOT StructureHub. StructureHub manages sessions, not creates them.

4. **Session tabs:** Multiple 3-window tabs open simultaneously (one per active creation session). NewStructure displays info based on which tab is active (two-way binding).

5. **Tab lifecycle:** Tab starts inactive; NewStructure's "Add/Create-New-Structure" button opens/activates it. Can be closed after "Add to Project" (Settings-configurable). NewStructure can stay open (also Settings-configurable).

6. **Add to Project location:** **In StructureHub only.** NewStructure has "Move-to-StructureHub" button instead. Structure saves to active ProjectTree folder as POSCAR, appears in ProjectTree automatically.

7. **Active folder:** Already implemented in ProjectTree (highlighted). Clicking folder makes it active. POSCAR written there.

No history/recent display in StructureHub; no changes to rest of app (wavefunctions, defect analysis, etc.).

---

## Act 2 — Codex Adversarial Review

_Ready to proceed to codex-cli review. Awaiting user sign-off to run Codex rounds._

## Round 1 — Codex

Codex identified 13 material issues:

1. Renderer contract violation — ephemeral sessions have no StructureId, violates domain contract. Need explicit session registry.
2. Three-window design inconsistent — spec alternates between 3-window, 1x2x1, 2x2 (one empty). Needs exact topology.
3. Session data model missing — multiple tabs + binding require CreationSession model with IDs, ownership, lifecycle.
4. Active-folder validation assumed, not implemented — ProjectTreePanel publishes raw paths, no project-root validation in coordinator.
5. Path traversal/name injection — "sanitized names" unspecified; job concatenates user text directly into path.
6. TOCTOU race conditions — concurrent saves, folder deletion, directory creation races unspecified.
7. Atomic-write incomplete — PoscarWriter shares `poscar_input.json`, no temp-file/collision policy defined.
8. Failure rollback incomplete — empty directories left, registration has no Result type, no fallback.
9. Draft loss on publish — session clears immediately after request, violates retry-on-failure contract.
10. Import/Analyze modes undefined — no input/validation semantics, existing import is blocking in Render().
11. Session closure unsafe — closing renderer tab leaves NewStructure/StructureHub pointing at dead window.
12. Settings schema vague — "configurable in Settings" has no keys, defaults, persistence, migration spec.
13. Testing underspecified — no explicit coverage for path containment, name sanitization, races, atomicity, rollback, session isolation.

VERDICT: REVISE

---

### Claude's response (Round 1)

Accepting all 13 findings. They are load-bearing for a production implementation. Revising PLAN.md to add:
- Explicit session data model (CreationSession)
- Path validation + sanitization contract
- Atomic write + rollback spec
- Session lifecycle transitions + events
- Settings schema (concrete keys)
- Explicit testing strategy (unit + integration + UI)
- Renderer contract alignment (ephemeral session registration)

These are not over-specification — they are architecture. Moving to Round 2.


---

## Round 2 — Codex

16 material issues found; many foundational. Summary by category:

**Path handling (3):**
1. Path containment reversed — prefix check is wrong direction
2. PathValidation layer confusion — can't validate project-root membership from Core
3. Symlink/junction vulnerabilities — "no symlinks" undefined; lexical prefix confusion

**Directory & file creation (3):**
4. Destination directory creation missing from spec
5. Crash-cleanup guarantee overstated — empty dir persists if creation succeeds but POSCAR rename fails
6. Temp-file retention policy contradicts itself (delete vs. retain vs. cleanup)

**Event & ordering (3):**
7. Session events missing sessionId + attemptId correlation
8. Success order violates domain contract (registration must precede success event, not follow)
9. Domain-registration failure underspecified (no fallible API, no cleanup defined)

**Behavior/UX clarity (4):**
10. Analyze Existing contradictory — described as read-only, then editable, then saving to library
11. Target folder — captured at session creation vs. currently selected folder (unclear which)?
12. Renderer toolbar routing undefined — are actions per-pane, broadcast, or session-level?
13. Session creation timing — mode selection vs. explicit creation; orphaned sessions possible

**Configuration (2):**
14. `max_concurrent_sessions` setting has no enforcement rule or rejection UI
15. PoscarWriter shared temp-file problem (poscar_input.json not unique per attempt)

**Testing (1):**
16. Filesystem security tests miss symlink/reparse/lexical-prefix cases

VERDICT: REVISE

---

### Claude's response (Round 2)

**Accepting:** path logic (1-3), directory creation (4), event correlation (7-9), behavior clarification (10-11), testing gaps (16).

**Deferring to later phases:** crash cleanup guarantee (5), temp retention policy detailed cleanup (6), toolbar routing specifics (12, requires UI testing), session creation timing refinement (13).

**Removing:** `max_concurrent_sessions` setting entirely (not worth the spec/testing cost for MVP; memory-limited by OS anyway). Remove from Settings schema.

**Specific revisions to PLAN.md:**

- Path containment: use component-wise ancestor check, not string prefix.
- PathValidation split: generic checks stay in Core, project-root authorization moves to App coordinator (StructureLifecycleCoordinator).
- Destination directory: explicitly created before temp write; documented as atomic create-or-fail.
- Analyze Existing: one path only — editable draft → Move to StructureHub → Add to Project. Remove material-library save from this plan.
- Target folder: captured at session creation; displayed in StructureHub before Add to confirm. If user changes ProjectTree selection after session creation, StructureHub shows warning.
- Event correlation: all lifecycle events carry sessionId + attemptId. Stale completions are ignored by coordinator.
- Domain registration: moved to main thread in coordinator, before ProjectStructureAdded is published. Registration API is `Result<StructureId>`.
- Failure cleanup: if registration fails, POSCAR is deleted (quarantine phase deferred). Crash cleanup defined as "best effort startup scan" (future phase).

Removing Round 2 issues 5, 6, 12, 13, 14 from MVP scope. They're valid concerns; documenting them as future work.


---

## Round 3 — Codex

7 material issues identified. Assessment by Claude:

**Accept & clarify immediately (3):**
1. **Event IDs:** Not all events need attemptId (only attempt-related ones do). SessionCreated, RendererTabClosed are sessionId-only. Clarifying event schemas in spec.
5. **Target-change warning:** Specify that session captures canonical target at creation; warning shown if ProjectTree current folder differs. Coordinator polls or registers listener.
7. **Event ownership:** Job-result event carries `Result<Path>`, `sessionId`, `attemptId` explicitly; coordinator matches and ignores stale attempts.

**Accept with deferred polish (3):**
2. **Rollback selective cleanup:** Remove only POSCAR + known temp files; delete directory only if empty. Implementation detail; add to Phase 4 spec.
4. **Structure validation:** Deferred to Phase 0 (define validation contract used by all modes). Not blocking MVP; can ship with basic validity checks.
6. **Atomicity crash guarantee:** Weaken criterion to "best-effort staging with uniquely named directory"; crash after creation leaves orphan, cleaned on next startup (deferred). Staging strategy detailed in Phase 4.

**Defer with documented scope (1):**
3. **Symlink/junction handling:** Phase 1 rejects symlinks in canonical-path check. Symlink-in-project-tree is future work; document as known limitation, not deferred polish.

All clarifications made in revised PLAN.md sections below. Moving to Round 4.


---

## Round 4 — Codex

6 material issues. Assessment by Claude:

**Accept & fix immediately (4):**
1. **Event table wording:** Register happens before publishing ProjectStructureAdded (coordinator action). Event handlers only update consumers. Clarifying table.
2. **Job authorization:** Job receives immutable authorized-root snapshot as context parameter. Re-validates targetDirectory against this root (second check inside job).
3. **Symlink rejection:** Not relying on canonical() to reject. Explicitly inspect each path component for symlink/reparse status before canonicalization. Platform-specific code required.
4. **Validation scheduling:** Adding explicit Phase 0 validation-contract definition task: finite cells, valid species, coordinate bounds, renderer/save preconditions.

**Accept with implementation care (2):**
5. **Session close during pending save:** Define session state machine: Draft → Submitted → Completing. Session not removed until all pending attempts complete or timeout. Document in Phase 3/4.
6. **Rollback race on delete:** Use unique staging directories (per sessionId + attemptId); only move to final location after registration succeeds. Staging directory remains (quarantine) if delete fails. Verification by inode/identity if delete is attempted.

All fixes applied in revised PLAN.md. Moving to Round 5.


---

## Round 5 — Codex (FINAL: MAX_ROUNDS=5)

6 consistency issues identified. Claude's assessment:

These are internal-consistency issues, not architectural flaws. Fixes:

1. **Atomic-write contract (Section 5):** Replace old direct-directory spec with staging-directory authoritative sequence. All acceptance criteria reference staging.
2. **Session removal timing:** State machine (Draft → Submitted → Completing → Closing) is authoritative. Removal waits for pending attempts or timeout.
3. **Core/App split in Phase 0:** Core: generic sanitization + symlink component inspection. App: root membership checks via `IsAncestor()`.
4. **Event table wording:** "Coordinator performs Domain::Register()" **before** event is published; consumers only update state.
5. **Symlink acceptance criterion:** Replace canonicalization language with explicit "component-by-component inspection + rejection."
6. **Registration-failure cleanup:** If Domain::Register() fails after staging succeeds: move staging to final, then delete POSCAR (or leave as quarantine if delete fails).

All fixes applied inline to PLAN.md.


---

## Round 6 — Codex (extended review, user-requested beyond original MAX_ROUNDS=5)

Codex read actual repo code this round (`StructureLifecycleCoordinator`, `PoscarWriter`, `write_poscar.py`,
`UIConfig`/serializer) rather than only the plan text. 14 material issues found:

1. Event schema conflict — canonical contracts doc doesn't have sessionId/attemptId yet; producers/consumers not updated.
2. Stale-completion check ordering — must compare (sessionId,attemptId) *before* Domain::Register, not after.
3. Retry race — a stale attempt's filesystem write can't be "ignored" after the fact; needs a generation gate.
4. TOCTOU claim overstated — validate-then-operate still races; true fix needs no-follow OS handles.
5. `write_poscar.py` uses `shutil.move` — can silently overwrite a destination created after the collision check.
6. Cross-device rename fallback (copy+delete) breaks the "atomic rename" guarantee.
7. `PoscarWriter` shared `poscar_input.json` — plan acknowledges but leaves unresolved (was only a "Risk").
8. `StructureLifecycleCoordinator` tracks jobs by JobId only; no failure event on JobSystem/Domain unavailable; leaks files.
9. Renderer contract says StructureId-only; ephemeral session-tagged windows aren't supported by current code.
10. Settings schema — `UIConfig`/serializer has no `structure_creation` section; would silently not persist.
11. Symlink policy contradiction — reject-before-canonicalize vs. canonicalizing registered roots is unresolved for a symlinked root.
12. Name validation missing Windows edge cases (reserved name + extension, trailing dot/space).
13. Structure validation is defined but not wired as a hard precondition anywhere.
14. Session-close/cancellation protocol underspecified — job can outlive tab close, still write/register.

VERDICT: REVISE

### Claude's response (Round 6)

All 14 are real — this round actually reads the code the previous 5 rounds only reasoned about
abstractly, so it caught contract/implementation mismatches the earlier text-only rounds couldn't.
Accepting all 14, with one added simplification not asked for by Codex:

**New constraint (kills races 2/3/14 by construction, not by CAS machinery):** only one attempt may
be in flight per session at a time. StructureHub disables "Add to Project" while state is
Submitted/Completing. This is cheaper than a generation/CAS scheme and sufficient for a
single-user desktop app — no concurrent retries are possible, so "stale completion" reduces to
"tab closed while the sole in-flight attempt is still running," which the close protocol below
handles explicitly.

Fixes applied to PLAN.md:
1. Elevated event-contract update from implied to an explicit Phase 0 task: amend
   `docs/structure-lifecycle-contracts-2026-09-07.md` (add sessionId/attemptId) before any consumer code.
2. Single-in-flight-attempt constraint (above) — stale-completion logic is now a same-session
   Closing-state check only, not cross-attempt arbitration.
3. Same as above.
4. TOCTOU downgraded from "protection" to documented best-effort mitigation; acceptance criteria reworded.
5. Section 5 rewritten to require a fail-if-exists rename primitive, not `shutil.move`/generic move.
6. Staging directory now required to be a sibling of destinationPath (same volume) — cross-device
   rename is a hard error, never a copy fallback.
7. Elevated from Risk #2 to a Phase 0 blocking task: `PoscarWriter`/`ScriptRunner` must accept a
   per-attempt unique JSON input path.
8. Added to Phase 4: replace JobId-only tracking with session/attempt-keyed records; explicit
   failure event on JobSystem/Domain unavailability.
9. Added to Phase 2: renderer contract amendment for session-tagged ephemeral windows is in scope,
   not an afterthought — same doc as the event-contract update in (1).
10. Added to Phase 0/4: extend `UIConfig` + serializer with a typed `structure_creation` section,
    with a round-trip (de)serialization test.
11. Resolved: registered project roots are canonicalized once at registration time and rejected if
    that canonical path itself is a symlink/junction; all later checks operate on that canonical
    root, so the "reject symlink components" rule applies uniformly to everything under it.
12. `ValidateAndSanitizeName` spec expanded: case-insensitive reserved-name match including with any
    extension (`con.txt`), reject trailing dot/space in the sanitized component (Windows semantics).
13. Structure validation is now a hard precondition at both "Move to StructureHub" submission and
    inside the job (not just "defined in Phase 0").
14. Session state machine's Closing state now has an explicit protocol: no new Add allowed once
    Closing; if an attempt is in flight, wait for its callback or a configurable timeout, then
    force-remove and log an orphan-quarantine entry for the future startup scan.

Moving to Round 7.

---

## Round 7 — Codex (same thread, resumed)

Confirms Round 6 fixes landed (unique input paths, fail-if-exists rename, typed settings, event-ID
amendment, renderer-contract amendment, validation gates, documented TOCTOU residual). 12 new/deeper
issues found:

1. Single-in-flight rule is UI-only (disabled button); nothing stops a forged/duplicate event.
2. Timeout protocol still lets an unregistered directory get committed (rename can land after force-removal).
3. `sanitizedName` isn't revalidated inside the job itself — trusts the caller.
4. `SessionClosed` (Section 2) still says "immediate removal," contradicting the Closing-state protocol.
5. Stale-gating description conflates the internal job-completion payload with the public `ProjectStructureAdded` event.
6. "`Domain registration is Result<StructureId>`" is asserted, but current `RegisterAsProjectMember` returns a reference, no failure API.
7. `renameat2(RENAME_NOREPLACE)` has no defined fallback for platforms without it (macOS/BSD/old Linux).
8. Windows name validation still misses `<`, `>`, `"`, control chars; 255 limit not defined as chars vs bytes.
9. Per-attempt PoscarWriter input JSON, if cleanup fails, can leak into the committed structure directory.
10. No fsync/durability step specified despite implying crash-safety.
11. `PuntukasBridge.ParseStructure()` doesn't exist on the actual bridge (it exposes `LoadStructure()`).
12. Orphan quarantine is "log a line," not a durable record a future startup scan could actually find.

VERDICT: REVISE

### Claude's response (Round 7)

All 12 accepted — these are correctness gaps, not style. Fixes:

1. `activeAttemptId` check-and-set moves into the coordinator's event handler (single-threaded main
   loop, so this is a plain reject-if-already-set, no CAS needed) — UI disabling is now defense in
   depth, not the enforcement mechanism.
2. Timeout protocol rewritten: force-removal only detaches the session from UI-visible state; the
   coordinator keeps a minimal pending-cleanup record (sessionId, attemptId) until the job's callback
   actually arrives. Late success → destination directory is deleted (it was never registered, so
   deleting it is always safe — no unregistered project data survives). Late failure → log only.
3. Job now revalidates `sanitizedName` via `ValidateAndSanitizeName` as its own step, independent of
   what the caller passed — defense-in-depth boundary, same principle as the existing double
   `IsAncestor` check.
4. Removed `SessionClosed`-as-immediate-removal from Section 2. There is exactly one close path
   (`RendererTabClosed`) and exactly one protocol (the Closing-state machine in Section 7); NewStructure
   panel close and renderer-tab-X both route through it.
5. Introduced explicit internal-only type: `AddStructureToProjectJobCompleted(sessionId, attemptId,
   Result<Path>)` — never published on EventBus, coordinator-local. Stale/activeAttemptId gating and
   `Domain::Register` both happen against THIS payload. `ProjectStructureAdded` is only ever
   constructed and published after both checks pass — by definition it never carries a stale attempt.
6. Added explicit Phase 0 task: wrap/change the domain-registration entry point so it returns
   `Result<StructureId>` with named failure cases (duplicate id, invalid structure, registry
   unavailable) — `RegisterAsProjectMember`'s current reference-returning signature does not support
   this and must change or be wrapped.
7. Narrowed platform promise: this project builds Windows-only (premake `vs2022`/MSBuild toolchain,
   per `CLAUDE.md`) — the no-replace-rename contract is specified and tested for Windows
   (`MoveFileExW` without `MOVEFILE_REPLACE_EXISTING`) only. No macOS/BSD/POSIX fallback is in scope;
   documented as a Windows-only guarantee rather than pretending portability.
8. Expanded rejected-character set: `<`, `>`, `"`, and all control characters (0x00-0x1F) added.
   Length limit defined explicitly as 255 UTF-16 code units (NTFS component-name semantics), not bytes.
9. Per-attempt input JSON moved OUT of `stagingPath` entirely — written to a separate scratch
   location keyed by `sessionId`/`attemptId` (never renamed, cleanup best-effort there is fine since
   it can never end up inside the committed directory regardless of cleanup outcome).
10. Added explicit durability disclaimer: the atomic-rename contract guarantees **visibility
    atomicity** (no reader ever sees a partial POSCAR), not **crash durability** (no fsync of file or
    parent directory before rename) — a crash before OS-level flush can lose the write; the user
    retries. This is an accepted, now-explicit limitation, not a silent gap.
11. Fixed API reference: `PuntukasBridge.LoadStructure()` (actual method) replaces the invented
    `ParseStructure()`. Added to Phase 0 verification list alongside the atomic-database check:
    confirm `LoadStructure()`'s return shape gives lattice + species breakdown needed for the
    Analyze-Existing display; if not, scope a follow-up bridge method rather than inventing one here.
12. Orphan quarantine is now a durable marker: on any quarantine event (rename failure, timeout with
    late success not yet resolved, etc.) the coordinator writes a small `.quarantine` marker file
    (attemptId, timestamp, reason) next to the orphaned directory — the future startup-scan phase has
    a filesystem-based source of truth, not just a log line.

Moving to Round 8 (final requested round).

---

## Round 8 — Codex (same thread, resumed — 3rd of 3 user-requested extra rounds)

Confirms Round 7 fixes landed. 8 new/deeper issues:

1. **Plan wrongly claims Windows-only.** `premake5.lua` has real `gmake2`/Linux support
   (`system:linux` filters, `DS_PLATFORM_LINUX`). Round 7's platform-narrowing was factually wrong.
2. `CreationSession` state invariant broken: `SessionReadyForStructureHub` sets `state=Submitted`
   with no `activeAttemptId`, contradicting "activeAttemptId set whenever Submitted/Completing."
3. `AddStructureToProjectJobCompleted` "coordinator-local, never published" has no defined delivery
   path — JobSystem's actual completion channel is an EventBus job event; not specified how the
   coordinator receives it.
4. Timeout/crash gap: if the coordinator itself crashes/exits after commit but before the
   orphan-delete, nothing records that the committed directory is unregistered — the `.quarantine`
   marker is only written on documented failure paths, not proactively.
5. `MoveFileExW` no-replace isn't fully specified (error handling, network-redirector behavior).
6. Session removal isn't idempotent — duplicate/out-of-order close events undefined.
7. `attemptId` ownership/generation is unassigned — UI event supplies it with no freshness/uniqueness rule.
8. TOCTOU acceptance test ("verify rejected or handled") permits either outcome — no actual oracle.

VERDICT: REVISE

### Claude's response (Round 8 — final of the 3 requested extra rounds)

Accepting all 8. #1 is a correction of my own Round 7 mistake (I asserted Windows-only without
grepping `premake5.lua` first — should have verified before narrowing scope; confirmed via grep
that `system:linux` filters and `DS_PLATFORM_LINUX` are real). Fixes:

1. Reverted the Windows-only narrowing. No-replace rename is now specified for both: Windows
   (`MoveFileExW` without `MOVEFILE_REPLACE_EXISTING`) and Linux (`renameat2(..., RENAME_NOREPLACE)`,
   available on the kernel/glibc versions this project already targets via its Linux build).
2. Added a `Ready` state: Draft → Ready (after "Move to StructureHub," tab open, no attempt yet) →
   Submitted (attemptId assigned, Add-to-Project in flight) → Completing → Success/Failed → Closing.
   `activeAttemptId` invariant now correctly reads "set iff state ∈ {Submitted, Completing}."
3. Specified delivery: `AddStructureToProjectJobCompleted` is not a new channel — it's how the
   coordinator's existing JobSystem `JobCompletedEvent` handler (keyed by the session/attempt map
   from the Phase 4 refactor) extracts the job's `Result<Path>` payload on the main thread. No new
   EventBus event type is added; this is a naming/documentation clarification of an existing path.
4. Added a durable sentinel: the job writes a `.pending_registration` marker inside stagingPath
   (so it survives the rename into destinationPath) as part of its normal write, and the coordinator
   deletes that sentinel only immediately after `Domain::Register` succeeds. A crash anywhere before
   that point leaves the sentinel in place. The (already-deferred) future startup scan's contract is
   now concrete: any structure directory containing `.pending_registration` is unregistered and a
   quarantine/re-registration candidate — this replaces "log a line and hope" with a filesystem fact
   that survives a coordinator crash, without requiring a full transactional journal.
5. Added explicit error handling: check the rename call's return value; on Windows,
   `ERROR_ALREADY_EXISTS` is treated as the collision/quarantine path already specified. Network
   redirectors (SMB/UNC targets) are explicitly out of scope/untested — the guarantee is local
   filesystem only (NTFS on Windows, the project's supported local filesystems on Linux).
6. Session removal made idempotent: `RendererTabClosed` for a `sessionId` not present in the
   registry is a no-op (logged, not an error). Settings-driven auto-close after a successful Add
   publishes this same event rather than calling a separate internal removal path — one close path,
   safe to invoke twice.
7. `attemptId` is no longer UI-supplied. The coordinator generates it atomically inside the
   `AddStructureToProjectRequested` handler, in the same step as the activeAttemptId check-and-set
   (check none active → generate uuid → set as activeAttemptId → spawn job). The public request
   event no longer carries an attemptId field; it's assigned server-side and flows outward from there.
8. Acceptance criterion reworded to a real oracle: "an attacker/race-created destination is never
   silently overwritten — the job's fail-if-exists rename must return a collision error in that
   case, verified by test, not merely 'rejected or handled.'"

**This closes the 3rd of the 3 extra rounds the user asked for (Rounds 6-8 beyond the original
MAX_ROUNDS=5).** Per skill rules, stopping the Codex loop here — handing the current state back to
the user rather than continuing indefinitely. Verdict remains REVISE; findings are narrowing in
scope each round (14 → 12 → 8) but a "found actual API/behavior mismatches by reading real code"
review can likely continue finding real things for a while longer, since this plan is deep enough
to touch many existing subsystems (JobSystem, PoscarWriter, UIConfig, Domain registration,
PuntukasBridge, premake platform config). That's a genuine signal this plan has non-trivial surface
area, not that it's unsound.
