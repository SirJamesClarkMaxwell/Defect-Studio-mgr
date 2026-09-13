# Plan Review Log: scene tools + group-theory panel
Act 1 (grill) complete — plan locked with the user 2026-09-13. MAX_ROUNDS=5.
PLAN_FILE=docs/work/project/plans/2026-09-13-scene-tools-and-group-theory.md (root PLAN.md belongs to
another workstream and is untouched).

Reviewer model: gpt-5.6-luna (~/.codex/config.toml) - codex-cli 0.152.1. THREAD_ID=01a09a1e-52e8-7212-98ce-d901d28d4180

## Round 1 — Codex

The plan is not ready to implement. Material problems:

1. **Stable atom references are currently not stable.** `task/20` preserves atom IDs by array index, while pins still store `atomIndices`; deleting, reordering, or inserting atoms can make a persisted reference point to a different atom.  
   Fix: add persistent atom IDs to the domain model and migrate all renderer/pin references to those IDs before persistence.

2. **Loading persisted IDs can immediately create collisions.** `SceneRegistry::CreateObject(existingId)` does not advance `m_NextObjectId`; the next newly created object can reuse an ID loaded from YAML.  
   Fix: seed the allocator above the maximum loaded ID and reject duplicate IDs during deserialization.

3. **Project-level persistence conflicts with per-window identity.** `SceneObjectId` is explicitly per-window, but `scene_objects.yaml` is project-level; multiple renderer windows, previews, or the same structure opened twice cannot be unambiguously represented.  
   Fix: persist an owning structure/window key, or make persisted IDs project-global.

4. **The persistence schema is underspecified.** “kind/transform/style” is insufficient for pins’ atom links/offsets, arrows’ endpoints and modes, and free labels’ text/rotation; a generic snapshot cannot round-trip these objects.  
   Fix: define a tagged per-kind payload schema with required fields, defaults, validation, and unknown-kind handling.

5. **The task-20 worktree is uncommitted, but the plan only says “finish, commit, merge.”** The actual sibling contains uncommitted scene changes that are not represented by the named branch merge.  
   Fix: explicitly reconcile and commit the sibling worktree’s diff, then test that commit before creating `dev`.

6. **The group-theory task is not actually disjoint from task 20.** The plan acknowledges concurrent panel-registration edits, so the “disjoint file set” assumption is false and merge order is underplanned.  
   Fix: designate one registration owner or land a shared panel-registration change before parallel work.

7. **The bridge extension has no concrete contract for most requested results.** The spike only returns decomposition and projected vectors; character tables, reducible characters, and `ActiveSpace` multiplets are not specified as request/result structs or tested schemas.  
   Fix: define the complete JSON/C++ contract and add one positive test per result family before implementing the panel.

8. **The clean-runtime dependency is omitted.** The spike documents that `ScriptRunner` uses the bundled runtime, not `.venv`; without explicitly updating dependency export, packaging, and runtime preparation, positive tests can fail or negative tests can pass for the wrong reason.  
   Fix: make groupy installation/runtime population a tracked task and add a clean bundled-runtime smoke test.

9. **Point-group detection is scientifically incomplete for the stated use case.** `site_symmetry_symbols` describes existing crystal sites, but a defect centre can lie between atoms; the plan leaves the NV⁻ case as an open question while making detection part of the implementation.  
   Fix: define a validated fallback using a cluster/point-group analyzer and make “undetermined” an explicit result.

10. **The basis coordinate frame is unspecified.** The bridge rotates positions about the origin, while the UI selects atoms around an arbitrary selection centre and periodic images may require minimum-image handling.  
    Fix: center/unwrap the basis around the selected centre before submission and persist the frame/tolerance used.

11. **Stale-result races are unhandled.** Recompute jobs can finish out of order after selection or structure changes; the older result can overwrite a newer analysis.  
    Fix: attach a monotonically increasing request/version token and discard results whose basis/structure revision no longer matches.

12. **The main-thread commit rule is not reflected in the group-theory workflow.** The plan says analysis runs through a job but does not specify how completion applies UI-visible state through the event bus/main thread.  
    Fix: deliver completion through a main-thread event carrying the job ID and revision, and commit only after validation on the main thread.

13. **Failure observability is too weak.** The existing job wrapper converts `StructuredError` into `runtime_error` text, while the panel needs distinguishable cases such as missing groupy, invalid basis, stale result, cancellation, and malformed output.  
    Fix: preserve error category/code/details through job completion and expose them to the panel and diagnostics.

14. **Undo ownership is undefined and likely incompatible with current structure.** The current label history is embedded in `RendererWindowState`, while atom edits use command-service commands; “one `UndoStack`” does not say whether it is per window, per project, or global, nor how add/delete/style/preset mutations participate.  
    Fix: specify stack ownership, command registration, lifetime, save-clean semantics, and coverage for every scene mutation before removing the label stack.

15. **Arrow-to-curve migration lacks an actual schema migration.** Task 27 says old `start/end` migrate on load, but task 24 may already serialize `SceneArrow`; no version transition, mixed-version behavior, or migration test is defined.  
    Fix: reserve a schema version and test old arrow YAML loading into the new two-point Bézier representation.

16. **Several geometric operations have no defined degenerate behavior.** Plane alignment can receive collinear/duplicate points; curves can have zero-length segments or closed-loop frame seams; arrowheads can exceed very short curve lengths.  
    Fix: specify validation, fallback geometry, clamping, and user-visible errors for each degenerate case.

VERDICT: REVISE
### Claude's response

Verified against code before acting: no stable atom id in Domain (true); `SceneRegistry::CreateObject(id)`
does not advance `m_NextObjectId` (true, SceneRegistry.cpp:38-48); `SceneObjectId` is per-window
(true, SceneObject.hpp:23); jobs rethrow `StructuredError` as `std::runtime_error` text (true,
GetSymmetryInfoJob.cpp:36 and 5 other jobs); `UndoStack` is one app-global instance in `CoreLayer`.

Accepted and applied:
- #2 allocator collision -> fixed in task 20 (advance past explicit ids, reject duplicates, test).
- #3 per-window vs project -> persist per structure entry, file-local ids remapped on load.
- #4 schema -> tagged per-kind payloads, defaults, unknown-kind preservation, skip-invalid.
- #5 uncommitted ds-task20 diff -> made explicit in step 1.
- #6 shared registration file -> task 20 merges first, task 23 rebases and owns the conflict.
- #7 bridge contract -> contract-first step with per-result-family tests.
- #8 runtime -> prepare_app_python_runtime dependency + failing (not skipping) smoke test.
- #9 detection -> pymatgen PointGroupAnalyzer on centred cluster + explicit Undetermined; NV test.
- #10 basis frame -> centre + minimum-image unwrap, frame/tolerances stored with result.
- #11 stale races -> revision token + structure revision + basis hash, discard mismatches.
- #12 main-thread commit -> completion via JobSystem/EventBus on main thread, validated commit.
- #13 error observability -> job preserves StructuredError categories.
- #14 undo ownership -> CoreLayer global stack, window key in command, full mutation coverage,
  label stack removed only after grep-checked migration.
- #15 arrow->curve migration -> formatVersion 2, v1 fixture test.
- #16 degenerate geometry -> guards specified for planes, Empty align, curves, heads.

Partially accepted:
- #1 stable atom ids: NOT adding persistent atom ids to CrystalStructure now - a domain-wide change
  (every add/delete/reorder path, POSCAR round-trip) out of proportion to the few objects that
  reference atoms. Instead: index+element+position validation on load and a visible "link broken"
  state, marked as a ponytail ceiling with the upgrade path named.

## Round 2 — Codex

Most prior findings are addressed: allocator collisions, merge ordering, schema tagging, runtime smoke tests, detection fallback, centering, stale-result checks, structured errors, undo ownership, migration, and degeneracy handling are now explicit.

Remaining material issues:

1. **Stable atom IDs remain a known correctness failure.** Index + element + position validation can still bind to the wrong atom after reorder when another atom matches those fields, and broken pins need a persisted frozen anchor to render meaningfully; the schema only says “atom refs.”  
   Fix: add `frozenAnchor`/broken-link state to `PinnedMeasurement` and explicitly accept or reject ambiguous matches rather than treating them as valid.

2. **The persistence schema contradicts its ownership model.** It says objects are per structure entry, but defines a top-level `objects:` list without a structure-keyed container.  
   Fix: define `structures: [{structureKey, objects: [...] }]` or use one file per structure.

3. **Unknown-kind preservation conflicts with “IO only plain data.”** Skipping unknown kinds while preserving them verbatim requires an opaque YAML/JSON payload retained by the in-memory persistence model; otherwise re-save loses them.  
   Fix: add an opaque-entry representation and round-trip test through load → runtime mapping → save.

4. **Persisted scene changes will not participate in project dirtiness.** Existing `StructureRecord::revision/savedRevision` tracks domain structure edits, and the plan explicitly adds no save-clean tracking, so adding or transforming a scene object may not mark the project dirty or trigger the expected save prompt.  
   Fix: increment a project/scene revision on every persisted scene mutation and update it atomically on successful save.

5. **The proposed undo behavior is unsafe.** A global `SceneObjectsSnapshotCommand` that becomes a logged no-op when its window is closed silently consumes the user’s undo operation and leaves the scene unchanged.  
   Fix: return a structured “target unavailable” error so the undo index remains unchanged, or retain/reopen the relevant scene state.

6. **The runtime dependency instruction names a mechanism that does not exist.** `prepare_app_python_runtime.py` copies the entire `.venv` site-packages and does not consume a dependency list; current `pyproject.toml` also has no groupy dependency.  
   Fix: add groupy/symengine to the authoritative dependency group and make export, runtime preparation, CI, and the smoke test use that same source.

7. **`ActiveSpace` is still an unverified external assumption.** The spike demonstrates groupy reduction/projection but does not establish the API, input contract, or output shape for the requested four-electron multiplets.  
   Fix: add a minimal ActiveSpace bridge spike/contract test before making it part of task 23’s acceptance scope.

8. **Minimum-image unwrapping is undefined for non-periodic structures and ambiguous cells.** The plan offers one basis path for both periodic and non-periodic structures but does not define the non-periodic branch or failure behavior for invalid/near-singular cells.  
   Fix: specify Cartesian-only handling for non-periodic structures and validate lattice invertibility before unwrapping.

9. **“Optional neighbour shell” is not reproducible enough for analysis provenance.** The result stores centre/frame/tolerances but not shell radius, selection policy, periodic-image choices, or species filtering, so the same saved analysis cannot necessarily be recomputed.  
   Fix: persist the complete basis-construction parameters and the resolved site list/hash.

10. **The NV⁻ detection test may validate only the ideal case.** The plan acknowledges tolerance-sensitive relaxed geometries but only requires an NV⁻ expectation of C₃ᵥ; it does not define whether a lower detected group is a warning, failure, or accepted result.  
    Fix: add ideal, relaxed, and intentionally distorted fixtures with explicit expected provenance/warning behavior.

VERDICT: REVISE
### Claude's response

Verified: `StructureRecord::revision/savedRevision` exist (Domain/ProjectWorkspace.hpp:21-22);
`UndoStack::Undo/Redo` return `Result<void>`; `pyproject.toml` has `[dependency-groups]` and no groupy;
groupy `ActiveSpace.from_orbitals(pg, irreps, nel)` + `term_table()` exist (groupy/multiplets/terms.py:192).

Accepted and applied:
- #1 frozenAnchor + linkBroken added to PinnedMeasurement schema; binding requires index AND element
  AND position-within-tolerance, so a same-element reorder fails the position check. Stable atom ids
  still deferred (reason unchanged from round 1).
- #2 schema now `structures: [{structureKey, objects}]`.
- #4 scene mutations bump StructureRecord::revision -> existing unsaved prompt covers them.
- #5 undo on missing window returns StructuredError, index unchanged.
- #6 groupy/symengine go into pyproject [dependency-groups] as the single source.
- #7 ActiveSpace contract test first; input is orbital irreps + nel (not site basis); multiplets drop
  out of task 23 rather than block it if serialization fails.
- #8 periodic: lattice invertibility check then minimum image; non-periodic: plain Cartesian.
- #9 neighbour shell removed (basis = exactly the selection); full provenance incl. resolved sites+hash.
- #10 ideal / relaxed-within-tolerance / distorted fixtures with explicit expected outcomes.

Rejected (simplified instead):
- #3 opaque unknown-kind preservation: dropped the preservation requirement altogether. Unknown kinds
  are skipped with a warning. Single-user desktop app; opening a newer project in an older build is not
  a workflow worth an opaque-payload layer in IO.

## Round 3 — Codex

Most round-2 findings are addressed: frozen anchors, structure-keyed persistence, dirty tracking, safe undo failure, dependency declaration, ActiveSpace preflight, periodic/non-periodic handling, deterministic basis provenance, and detection fixtures are now specified.

Remaining material problems:

1. **The plan contradicts itself on unknown-kind handling.** The schema says unknown entries are skipped and not preserved, but the test list still requires an “unknown-kind preservation” test.  
   Fix: change the test to assert warning + omission, or restore an explicit preservation requirement.

2. **Per-structure persistence still has multi-window consistency hazards.** Two windows for the same structure each hold separate runtime scenes; mutating one bumps the shared structure revision, but the other can later save stale scene state or issue undo commands against a different runtime copy.  
   Fix: define one canonical scene owner per structure and propagate/reload scene mutations to all open windows before save/undo.

3. **The local groupy dependency is not reproducible.** A local-path dependency points outside the repository, so a clean checkout or CI machine cannot sync `pyproject.toml` without an undocumented external directory.  
   Fix: pin groupy to a repository-accessible source/commit or make the external source an explicit, validated setup prerequisite with a CI install test.

4. **Multiplets are still allowed to disappear from the requested feature.** The plan’s goal and NV acceptance require multiplets, but task 23 may “drop” them if serialization is difficult, allowing a central requirement to be silently deferred.  
   Fix: either make multiplet serialization a hard acceptance criterion or split it into a named follow-up and remove it from this plan’s goal/acceptance claims.

5. **The dirty-tracking rule does not define save failure/undo ordering.** It says scene mutations and undo/redo bump `revision`, while `savedRevision` is updated by the existing save path; a failed save, undo after save, or redo after save needs precise revision semantics to avoid falsely clean projects.  
   Fix: specify that `savedRevision` changes only after successful scene-file and manifest commits, and add failed-save plus undo-after-save tests.

6. **The “full provenance” is only in memory.** Closing and reopening loses the chosen centre/frame/tolerances/site list/hash unless the analysis result itself is persisted, so recomputation and stale-result explanations are not reproducible across sessions.  
   Fix: either persist analysis provenance/results per structure or explicitly scope reproducibility to the live session and remove any implied cross-session guarantee.

VERDICT: REVISE
### Claude's response

Verified: `punktukas-tools` is also absent from `pyproject.toml` (installed into `.venv` from a local
checkout) - that is the existing convention for the user's own packages. Opening the same file twice
does create a second window (`RendererLayer.cpp:339-354`); whether both share a `StructureId` is not
yet known.

Accepted and applied:
- #1 test list contradiction fixed: unknown kind skipped + absent after re-save.
- #4 multiplets made a hard acceptance criterion; the "drop out" escape removed.
- #5 revision monotonic (undo/redo bump it); savedRevision only after scene file AND manifest written
  (temp + rename); failed-save and undo-after-save tests added.
- #6 analysis results scoped to live session explicitly; cross-session persistence added to Out of scope.

Partially accepted:
- #2 multi-window: not building a canonical shared scene per structure now. Task 24 first verifies
  whether windows can share a StructureId; if so, save writes the most recently focused window's
  objects and warns about the others. Undo commands key on window id (not structure), which removes the
  wrong-copy undo hazard. Canonical shared scene is a named ponytail ceiling + Out of scope.
- #3 groupy reproducibility: rejected the local-path pyproject entry (it is exactly the
  non-reproducible thing flagged). Followed the punktukas convention: documented setup prerequisite +
  a ScriptRunner smoke test that fails with a message naming the setup step. symengine (on PyPI) goes
  into scientific-core. No CI exists, so no CI install test.

## Round 4 — Codex

The round-3 findings are mostly addressed. The unknown-kind test, hard multiplet requirement, save-revision rules, failed-save tests, and live-session provenance scope are now consistent. The local groupy setup is an intentional project convention and is adequately documented for this workflow.

Material problems remain:

1. **The accepted multi-window behavior still causes intentional data loss.** If two windows share a `StructureId`, saving only the most recently focused window discards scene edits made in the other window; logging a warning does not recover those edits.  
   Fix: block saving until the windows are reconciled, merge their scene objects deterministically, or make one canonical scene the source of truth before persistence.

2. **Two-file “atomic” saving is not actually atomic.** Temp-file-plus-rename for `scene_objects.yaml` and `manifest.yaml` can leave one file replaced and the other untouched after a crash or rename failure, producing a mismatched project state.  
   Fix: use a transaction/journal or backup-and-recovery protocol that commits and recovers both files as one generation.

3. **The plan still contains a stale contradiction in its test list.** The schema says unknown kinds are skipped and absent after re-save, but the test list still says “unknown kind preservation” in the current plan text.  
   Fix: rename that test to “unknown kind skipped with warning and absent after re-save.”

VERDICT: REVISE
### Claude's response

Accepted and applied:
- #1 multi-window data loss: replaced "most recently focused window wins" with a union of all windows'
  objects for that StructureId, plus a divergence warning. No edit is dropped; worst case a duplicate.
- #2 two-file atomicity: made the design explicit instead of adding a journal. The manifest does not
  reference scene_objects.yaml and neither file's validity depends on the other, so a crash between the
  renames still yields a loadable project; savedRevision unchanged keeps it dirty. A generation journal
  would protect an invariant that does not exist.

Rejected:
- #3 stale test-list text: not present. Grep of the plan for "preserv" returns only unrelated lines
  (job preserves StructuredError; "not preserved on re-save"; behaviour-preserving tests). The test list
  was already changed in round 3 to "unknown kind skipped with a warning and absent after re-save".

## Round 5 — Codex

Round-4 finding #3 is addressed: the plan’s test now says unknown kinds are skipped with a warning and absent after re-save; “preservation” only appears in unrelated contexts.

The multi-window fix introduces a new material bug:

1. **The proposed union duplicates unchanged objects.** Each window remaps file-local IDs to distinct per-window runtime IDs. Opening the same persisted scene twice therefore gives both windows identical objects with different IDs; blindly unioning them on the first save creates duplicates even without divergence.  
   Fix: retain a persisted-object identity/source revision per window and union only additions/changes, or use one canonical scene owner.

2. **The two-file save ordering can still lose in-memory scene edits on a crash.** If `manifest.yaml` is renamed successfully and `scene_objects.yaml` fails or the process dies before its rename, the project remains loadable but reopens without the latest scene edits; “loadable” does not mean lossless.  
   Fix: commit `scene_objects.yaml` first and the manifest second, with recovery/backup metadata or a retryable journal for the scene file.

3. **The dirty state is not durable across restart after a partial save.** `revision`/`savedRevision` are runtime fields; if the process dies after one rename, the next process cannot rely on the unchanged in-memory `savedRevision` to show the project dirty.  
   Fix: persist a save generation/checksum or recovery marker and clear it only after both writes complete.

VERDICT: REVISE
### Claude's response (MAX_ROUNDS reached - NOT re-reviewed by Codex)

Accepted and applied (unreviewed):
- #1 union duplicates unchanged objects: correct, the round-4 fix was wrong. Added a per-object
  `persistKey` (random 128-bit hex, stable across load/save and shared by windows loading the same
  file); union is deduplicated by persistKey, most recently focused window wins per key.

Partially accepted:
- #2 save ordering: scene_objects.yaml now written first, manifest second. No journal/backup: a crash
  mid-save loses the unsaved edits exactly as a crash before save would; the on-disk project stays
  loadable and consistent (no cross-file invariant).

Rejected:
- #3 durable dirty marker: after a process crash the in-memory edits are gone regardless, so a
  persisted "dirty" marker would flag a project whose on-disk state is already self-consistent and has
  nothing to recover. No recovery feature exists or is in scope.

## Resolution
DEADLOCK at MAX_ROUNDS=5 (last verdict REVISE). Open for the user: #2/#3 (crash-safety of save -
journal vs. none) and whether the round-5 persistKey fix is acceptable without another review round.
