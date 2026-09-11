# Plan: Structure Lifecycle Refactor — Steps 10-11 Redesign

_Locked via grill — by Claude Haiku + user (pzabier@gmail.com)_

## Goal

Redesign Structure Hub and NewStructure panels to provide 4 distinct creation modes (from template, from scratch, analyze existing, import file), each with a unified 3-window resizable renderer layout for supercell preview and hxkxl configuration. Structure Hub coordinates creation sessions and manages "Add to Project" workflow; NewStructure handles mode selection and has "Move to StructureHub" button. After commit, structure writes to active ProjectTree folder as POSCAR and appears in ProjectTree automatically.

## Data Model

### CreationSession
```
struct CreationSession {
  uuid sessionId;                        // Unique ephemeral session identifier
  enum mode { FromTemplate, FromScratch, AnalyzeExisting, ImportFile };
  CrystalStructure draftStructure;       // Current working structure (may be invalid)
  string displayName;                    // User-provided name (validated, sanitized)
  Path targetDirectory;                  // Active ProjectTree folder (validated)
  std::vector<RendererWindowId> previewWindowIds;  // 3 ephemeral renderer windows (NO StructureId — not in domain yet)
  bool dirty;                            // Unsaved changes flag
  enum state { Draft, Ready, Submitted, Completing, Success, Failed, Closing };
  // Draft -> Ready (after "Move to StructureHub": tab open, no attempt yet)
  //       -> Submitted (attemptId assigned, Add-to-Project in flight)
  //       -> Completing -> Success|Failed -> Closing.
  // Invariant: activeAttemptId is set iff state in {Submitted, Completing} — Ready has no attempt yet.
  // Closing waits for any pending attempt to finish (or timeout) before the
  // session is removed from the registry — see RendererTabClosed handling.
  optional<StructuredError> lastError;   // Preserved for retry
  optional<uuid> activeAttemptId;        // Set while Submitted/Completing; empty otherwise
  timestamp createdAt;
  timestamp lastModifiedAt;
};
```

**Single-in-flight-attempt constraint:** a session has at most one `activeAttemptId` at a time.
**Enforced by the coordinator, not just the UI:** on `AddStructureToProjectRequested`, the
coordinator's event handler checks `session.activeAttemptId` and rejects (with a structured error,
no job spawned) if one is already set — a plain check-and-set, no CAS needed since the coordinator
runs on the single main thread. StructureHub disabling the "Add to Project" button is defense in
depth on top of this, not the enforcement mechanism itself. This removes the need for a
generation/CAS scheme to arbitrate concurrent retries — there are none, by construction — and
reduces "stale completion" to a single case: the renderer tab closed while the sole in-flight
attempt was still running (handled explicitly by the Closing-state protocol below).

Invariants:
- `sessionId` is globally unique for the lifetime of the app.
- `targetDirectory` is revalidated before every write (not cached).
- `previewWindowIds` are ephemeral; renderer windows close when session closes or Add-to-Project succeeds.
- `draftStructure` persists after submission failure (supports retry).
- Only main thread creates/modifies sessions.

---

## Approach

### 1. Session Registry & Lifecycle (NEW)

- Add `CreationSessionRegistry` in `App/` (composition root owns it).
- NewStructure registers a session on mode selection.
- StructureHub reads all active sessions and displays them.
- Renderer tabs are managed by session lifecycle, not free-floating.
- Session closed event → clears NewStructure + StructureHub references.
- Session submitted event → StructureHub marks it "Pending".
- Session success/failure event → StructureHub updates display, preserves draft if failed.

---

### 2. NewStructure Panel Refactor

- Add mode selector as a **tab bar inside the NewStructure panel** with four tabs, one per
  `CreationMode`: **"Create New" | "From Library" | "Import File" | "Analyze Existing"**.
  The first three already existed in the application (in StructureHubPanel) and were removed by
  commit `86ef70e`; the Hub is now the session manager, so they belong here. Mapping:
  `Create New -> FromScratch`, `From Library -> FromTemplate` (prototype/material catalog),
  `Import File -> ImportFile`, `Analyze Existing -> AnalyzeExisting`.
- Each mode has its own form, **validates independently** (no blocking I/O in Render()).
- Mode "Import File": file selection + parsing moved to async job; Render() displays progress/status.
- Mode "Analyze Existing": call PuntukasBridge to parse, display lattice + atoms, allow editing.
- Add "Move to StructureHub" button (publishes `MoveSessionToStructureHub` event; creates ephemeral renderer tab).
- Two-way binding: reads active session ID from registry; updates draft structure on form changes.
- On close: publishes `RendererTabClosed` (the single, shared close event — same one the renderer
  tab's own X button publishes; NewStructure panel close and renderer-tab close both route through
  the one Closing-state protocol in Section 7, never an immediate unconditional removal).

---

### 3. Renderer Layout — Exact Specification

**Layout: 2+1 split (2 windows top, 1 bottom, resizable separator)**
- Top pane: 2 renderer windows side-by-side (e.g., original + transformed supercell)
- Bottom pane: 1 renderer window (full width)
- All 3 share a single vertical toolbar (left) + horizontal toolbar (top).
- Separators are draggable; proportions persist in session state.
- **Per-view visibility is user-controlled**: three checkboxes in the NewStructure panel
  ("Unit cell", "Supercell", "Analysis") toggle each of the three views independently. A hidden view
  releases its ephemeral renderer window; re-checking it rebuilds the window and re-docks it into
  its fixed slot. The 2+1 *arrangement* stays fixed (not user-configurable) - only which of the
  three slots are occupied is configurable. This supersedes the earlier "layout is fixed, nothing
  configurable" wording: the slot geometry is fixed, the slot occupancy is not.
- The tab hosts a **normal renderer window**: one vertical toolbar (left) and one horizontal toolbar
  (top) shared by all three views. Per-view toolbars are a bug, not a layout - the three views are
  panes inside one renderer window, not three independent renderer windows.
- Tab title: `[Session] Mode — (h×k×l)`, e.g., `Session-xyz From Template — (2×2×2)`.
- Each renderer window is ephemeral (no domain StructureId) and tagged with `sessionId`.
- On tab close: publishes `RendererTabClosed` event; session handles cleanup.

---

### 4. Path Validation & Sanitization (NEW)

**Generic validation (Core layer — PathValidation module):**
```
function ValidateAndSanitizeName(userInput: string) -> Result<string> {
  // Reject:
  // - Empty or all-whitespace
  // - Separators / invalid NTFS chars: / \ : | ? * < > " and all control chars (0x00-0x1F)
  // - Traversal: . .\ .. ..\ ~ 
  // - Reserved (Windows, case-insensitive, WITH or WITHOUT any extension):
  //   CON, PRN, AUX, NUL, COM0-9, LPT0-9 — "con.txt" and "CoM1.tar.gz" are both rejected
  // - Trailing dot(s) or trailing space(s) in the component (Windows strips these silently,
  //   which can make two visually-different names collide on disk)
  // - Length > 255 UTF-16 code units (NTFS component-name limit — chars, not bytes)
  // Return: cleaned string, safe to use as a directory component
}

function IsAncestor(canonicalAncestor, canonicalPath) -> bool {
  // Component-wise comparison, not string prefix.
  // Resolves both to absolute canonical form (no symlinks, no .).
  // Returns true iff ancestor is a directory component prefix of path.
  // Rejects lexical tricks (C:\project vs C:\project-other).
}
```

**Project-root authorization (App layer — StructureLifecycleCoordinator):**
```
function ValidateProjectTarget(proposedPath, registeredProjectRoots) -> Result<Path> {
  // 1. Resolve to canonical path (reject symlinks/junctions in proposedPath's own components).
  // 2. Check it exists and is readable.
  // 3. For each registered root: if IsAncestor(root, proposedPath) → valid.
  // 4. Return canonical path or "not under any project root" error.
  // Executed twice: on selection change + inside job before write.
}
```

**Symlinked project-root policy (resolves Round 6 contradiction):** a project root is canonicalized
exactly once, at registration time, and registration is REJECTED if that canonical root path is
itself a symlink/junction. Every later check (selection, submission, in-job revalidation) operates
against that already-canonical, already-non-symlink root — so "reject symlink components before
canonicalizing" and "roots are canonicalized" are the same policy applied at different times, not
two competing ones. A symlink appearing *under* a registered root (not the root itself) is still
rejected by the component-inspection step, per the existing out-of-scope note on symlinked project
trees.

Split responsibility: Core owns sanitization, App owns authorization.

---

### 5. Atomic Write & Rollback (NEW)

**Contract:**
```
struct AddStructureToProjectJob {
  uuid sessionId;
  uuid attemptId;  // Unique per retry; used to ignore stale completions
  CrystalStructure structure;
  string sanitizedName;
  Path targetDirectory;      // Validated, canonical (revalidated here)
  Path authorizedRootSnapshot;  // Immutable project-root context from coordinator

  Result<Path> Run() {
    // This is the SOLE authoritative write contract — staging-directory based.
    // No direct-in-place write is ever performed.

    1. Revalidate targetDirectory against authorizedRootSnapshot (IsAncestor, second check).
       Reject if any path component is a symlink/reparse point (component-by-component inspection).
    1b. Revalidate sanitizedName via ValidateAndSanitizeName() again, independent of the caller —
        the job never trusts a pre-sanitized string handed to it; this is its own security boundary.
    2. Construct destinationPath = targetDirectory / sanitizedName.
    3. If destinationPath already exists → error (collision), no filesystem changes made.
    4. Create a unique staging directory, REQUIRED to be a sibling of destinationPath (i.e. also
       directly under targetDirectory) so the later rename is guaranteed same-volume/same-device:
       stagingPath = targetDirectory / "_poscar_staging_<sessionId>_<attemptId>_<uuid>"
       (create-or-fail; collision here is treated as a transient error, job may retry with new uuid).
    5. Write structure to stagingPath/POSCAR via `PoscarWriter`, passing a per-attempt unique input
       path living OUTSIDE stagingPath entirely — e.g. a scratch directory keyed by
       `<sessionId>_<attemptId>` — never the shared `install/users/default/temp/poscar_input.json`
       and never a file inside stagingPath itself. Keeping it outside stagingPath means a failed
       cleanup of that scratch file can never leak it into the committed directory (stagingPath, and
       therefore destinationPath after rename, only ever contains POSCAR). This requires the
       `PoscarWriter`/`ScriptRunner` API to accept a caller-supplied input-path (Phase 0 blocking
       task, see below). Cleanup of the scratch input file is best-effort and does not gate the job's
       success/failure result.
    5b. Write a `.pending_registration` sentinel file inside stagingPath (survives the rename into
        destinationPath). The coordinator deletes this sentinel only immediately after
        `Domain::Register` succeeds (Section 7). Its presence in a structure directory means
        "written to disk but never confirmed registered" — the durable fact a crash-recovery
        startup scan (deferred implementation, contract defined here) uses to find unregistered
        directories without needing the coordinator to have survived long enough to log anything.
    6. On write failure:
       - Delete stagingPath entirely (best-effort).
       - Return error. destinationPath was never created; no cleanup needed there.
    7. On write success:
       - Rename stagingPath → destinationPath using a fail-if-destination-exists primitive per
         platform this project actually builds for (`premake5.lua` has real `system:linux`
         (`gmake2`, `DS_PLATFORM_LINUX`) support alongside Windows — narrowing to Windows-only in an
         earlier draft of this plan was a mistake, corrected here):
         - **Windows:** `MoveFileExW` WITHOUT `MOVEFILE_REPLACE_EXISTING`. Check the return value;
           `ERROR_ALREADY_EXISTS` is the collision/quarantine path below. Local NTFS only — network
           redirectors (SMB/UNC targets) are explicitly out of scope/untested.
         - **Linux:** `renameat2(..., RENAME_NOREPLACE)`.
         Never `std::filesystem::rename` used bare on either platform — it silently overwrites an
         existing destination, reopening the collision window this step exists to close.
       - Same-volume placement (step 4) guarantees this is a single-volume rename; a cross-volume
         result is treated as an unexpected fatal error, never a copy-then-delete fallback.
       - If rename fails (destination now exists, or any other reason): leave stagingPath in place,
         write a `.quarantine` marker file next to it (attemptId, timestamp, reason — see Section 7
         Closing-protocol quarantine note), return error. Never delete stagingPath on a failed
         rename — it holds the only copy of the written data pending investigation.
    8. Return destinationPath/POSCAR or error.

    Durability note: this contract guarantees **visibility atomicity** — no reader ever observes a
    partially-written POSCAR — NOT crash durability. No fsync of the file or parent directory is
    performed before rename; a crash before the OS flushes to disk can lose the write entirely, in
    which case the user retries. This is an accepted, explicit limitation, not an implied stronger
    guarantee.

    Invariant: destinationPath exists with a valid POSCAR only if Run() returned success.
               On any Run() failure, destinationPath is never created or modified.
               A leftover "_poscar_staging_*" directory only occurs on rename failure or a crash
               mid-step-7; both are quarantine cases (marked with a `.quarantine` file) handled by a
               future startup scan, never silently deleted.
  }
};
```

Job returns `Result<Path>`. Coordinator must check Result before publishing success event.
Staging-directory write is the only atomic-write contract in this plan; the acceptance criteria,
Phase 0, and Phase 4 checklists below all refer back to this sequence — there is no separate
"direct write" variant anywhere else in the plan.

---

### 6. StructureHub Panel — Session Manager

- Display all active sessions (registry).
- Highlight active session (currently rendered tab).
- **Only StructureHub has "Add to Project" button** (applies only to active session).
- On "Add to Project" click: validate name + folder again, submit `AddStructureToProjectRequested` event.
- Listen to session lifecycle events (success/failure) and update UI.
- Failed sessions: keep draft + error in UI; user can edit and retry.
- No recent/history display.

---

### 7. Workflow Integration & Event Flow

**Key invariant:** Domain registration happens BEFORE success event is published.

```
User selects mode in NewStructure
  → NewStructure.OnModeSelected() creates session + registers it
  → Publishes SessionCreated(sessionId)
  
User fills form, clicks "Move to StructureHub"
  → StructureHub captures targetDirectory (current active ProjectTree folder)
  → Publishes SessionReadyForStructureHub(sessionId, targetDirectory)
  → App layer: creates renderer tab, tags it with sessionId; session.state = Ready (no attemptId yet)
  → NewStructure two-way binding: reads/updates active session draft
  
User in StructureHub, clicks "Add to Project"
  → StructureHub reads session's captured targetDirectory
  → StructureHub compares captured target to current ProjectTree selection (canonical path comparison)
  → If different: display warning "Target folder has changed" + show original target + option to use new target or keep original
  → User confirms (keeps original or switches to new target)
  → Validates name + targetDirectory (2nd check, coordinator via ValidateProjectTarget)
  → Publishes AddStructureToProjectRequested(sessionId, structure, name, targetDirectory)
    — no attemptId here; the UI doesn't own attempt identity, the coordinator does (next step)
  → Coordinator's handler, atomically (single main thread step):
    1. Reject if session.activeAttemptId is already set (single-in-flight enforcement)
    2. Generate a fresh attemptId, set it as session.activeAttemptId, session.state = Submitted
    3. Spawn AddStructureToProjectJob(sessionId, attemptId, ...) via JobSystem
  
Job runs in background, calls Run():
  → If Run() returns success Path:
    → Job completes with internal-only AddStructureToProjectJobCompleted(sessionId, attemptId, Result::success(path))
  → If Run() returns error:
    → Job completes with internal-only AddStructureToProjectJobCompleted(sessionId, attemptId, Result::error(err))
  (AddStructureToProjectJobCompleted is not a new EventBus event type — it names how the coordinator
   consumes JobSystem's existing `JobCompletedEvent`: the Phase 4 session/attempt-keyed job-tracking
   map resolves `JobCompletedEvent.jobId` back to `(sessionId, attemptId)` and extracts the job's
   `Result<Path>` payload on the main thread. This is deliberately distinct from the public
   ProjectStructureAdded/Failed events below, so the coordinator's stale/activeAttemptId gating and
   Domain::Register both happen BEFORE any public event is even constructed. ProjectStructureAdded,
   once published, is by definition already registered and already current — it never carries a
   stale attempt.)

App coordinator (main thread) receives AddStructureToProjectJobCompleted:
  → Checks attemptId == session.activeAttemptId (and session.state != Closing) FIRST, before
    touching Domain at all. If it doesn't match: this is the Closing/timeout case — see the
    Closing-state protocol below, no public event is published.
  → If it matches and result is success (staging already renamed to destinationPath by the job — see Section 5):
    1. Call Domain::RegisterStructureInProject(structure) → Result<StructureId>
       (this requires wrapping/changing the current `RegisterAsProjectMember` API, which returns a
       bare reference with no failure signal today — Phase 0 blocking task, see Implementation phases)
    2. If registration fails: delete destinationPath/POSCAR (best-effort); if delete fails, leave
       destinationPath in place — its `.pending_registration` sentinel (Section 5) already marks it
       as quarantine-worthy for the future startup scan, so no separate marker file is needed here.
       Publish ProjectStructureAddFailed(sessionId, attemptId, error).
    3. If registration succeeds: delete the `.pending_registration` sentinel from destinationPath
       (this is the confirmation step the startup scan's contract depends on), then publish
       ProjectStructureAdded(sessionId, attemptId, newStructureId, poscarPath)
    → ProjectTree updates automatically (structure appears)
    → StructureHub: marks session "Success"
    → NewStructure: optionally resets (configurable in Settings)
    → 3-window tab: optionally closes (configurable in Settings)
  
  → If failure:
    → Publish ProjectStructureAddFailed(sessionId, attemptId, error)
    → StructureHub: displays error, preserves draft for retry
    → NewStructure: shows error, user can edit and retry
    → Tab stays open
  
Tab closed (user clicks X) — Closing-state protocol (also entered from NewStructure panel close per
Section 2, AND from Settings-driven auto-close after a successful Add — all three publish this same
event; the handler is idempotent: if sessionId is already absent from the registry, no-op + log,
never an error, so duplicate/out-of-order close events are safe):
  → Publishes RendererTabClosed(sessionId); session.state = Closing
  → No new AddStructureToProjectRequested is accepted for a Closing session (coordinator-enforced,
    see single-in-flight-attempt constraint in Data Model).
  → If session.activeAttemptId is empty (no attempt in flight): remove session from registry immediately.
  → If an attempt IS in flight:
    - Session is detached from UI-visible state immediately (NewStructure/StructureHub stop
      referencing it, freeing the "slot" a user perceives) — but the coordinator keeps a minimal
      pending-cleanup record `{sessionId, attemptId}` alive until the job's
      AddStructureToProjectJobCompleted payload actually arrives or a configurable timeout
      (default e.g. 30s) elapses first.
    - On payload arrives before timeout:
      - Success → destinationPath now exists but is UNREGISTERED (Domain::Register is skipped for a
        Closing session, per the check above) → coordinator deletes destinationPath (safe: nothing
        ever referenced it, it was never domain-registered) → pending-cleanup record discarded.
      - Failure → nothing on disk to clean up (job's own failure path already cleaned its staging
        dir) → pending-cleanup record discarded, error logged.
    - On timeout (payload not yet arrived): pending-cleanup record is NOT discarded — it stays
      alive specifically to catch the late payload when it eventually arrives (the job itself is
      not forcibly killed; JobSystem cooperative-cancel is out of scope here). When the late payload
      does arrive, the same success/failure handling above runs against the still-alive
      pending-cleanup record. This means an unregistered destinationPath can never survive
      unresolved — it is always either deleted (late success) or was never created (late failure);
      the only persistent artifact from a timeout is a `.quarantine` marker file if the coordinator
      itself cannot complete the cleanup (e.g. delete fails), for the future startup scan.
```

**Event contracts updated:**

| Event | sessionId | attemptId | Coordinator Action |
|-------|-----------|-----------|-------------------|
| `SessionCreated` | yes | — | register session in CreationSessionRegistry; state=Draft |
| `SessionReadyForStructureHub` | yes | — | create renderer tab; state=Submitted |
| `AddStructureToProjectRequested` | yes | — (assigned by coordinator on receipt, not carried inbound) | reject if activeAttemptId already set; else generate attemptId, set activeAttemptId, spawn AddStructureToProjectJob; state=Submitted |
| `AddStructureToProjectJob::Complete(Result)` | yes | yes | (internal job result, not published) |
| `ProjectStructureAdded` | yes | yes | Coordinator checks attemptId == session.activeAttemptId BEFORE calling Domain::Register; if session is Closing/attemptId stale, skip registration, treat write as orphaned (Closing protocol); else **Coordinator performs** Domain::Register (before publishing), then update consumers (ProjectTree, etc.); state=Completing→Success |
| `ProjectStructureAddFailed` | yes | yes | if attemptId == session.activeAttemptId, display error + preserve draft; state=Completing→Failed; else (session Closing) log only, per Closing protocol |
| `RendererTabClosed` | yes | — | wait for pending attempts to complete (or timeout); then remove session from registry; state=Closing |

- Non-attempt events (`SessionCreated`, `RendererTabClosed`) carry sessionId only.
- Attempt-related events carry sessionId + attemptId, EXCEPT the inbound `AddStructureToProjectRequested` (UI intent), which carries only sessionId — attemptId is coordinator-assigned on receipt, never client-supplied (Section 7).
- Domain registration is `Result<StructureId>`, not void.
- Success event published only after successful domain registration (on main thread, before ProjectStructureAdded).

---

### 8. Import & Analyze Modes (SPECIFIED)

**Import File:**
- UI: file browser + progress indicator.
- Job: LoadStructureFromFileJob (async, cancellable, uses PuntukasBridge).
- On success: result structure loaded into draft; UI shows lattice/atoms.
- On failure: error displayed; user can retry with different file.

**Analyze Existing:**
- UI: file browser (load existing POSCAR from disk or ProjectTree).
- Parsing: `PuntukasBridge.LoadStructure()` (actual current API — verify its return shape gives the
  lattice + species breakdown this display needs; if not, scope a follow-up bridge method rather
  than inventing one here, see Phase 0 verification task).
- Display: lattice info, atoms, optional bond visualization.
- Editability: allow atom position tweaks, supercell hxkxl changes (optional; can be read-only in MVP).
- Workflow: editable draft → "Move to StructureHub" → Add to Project (same convergence as other modes).
- Note: Saving to material library is a separate workflow, not in scope for this plan.

**Structure validation is a hard precondition, not a soft check:** the Phase 0 validation contract
(finite non-degenerate cell, valid species, valid coordinates, renderer-safe) is enforced at (a) the
"Move to StructureHub" button — disabled/erroring on an invalid draft — and (b) again inside
`AddStructureToProjectJob::Run()` before any staging write. A draft may be transiently invalid while
the user is mid-edit in NewStructure, but it can never cross either of those two gates while invalid.

---

### 9. Settings Schema (NEW)

```yaml
# install/users/default/config/ui_settings.yaml
structure_creation:
  close_renderer_tab_after_save: true  # bool, default true
  reset_new_structure_after_save: false  # bool, default false
```

Persistence: YAML in user config directory. Defaults if missing. Migration: no version yet (v1 implicit).

**Removed from MVP:** `max_concurrent_sessions` (memory-limited by OS; not worth config overhead). Removed `temp_file_retention_on_failure` (all temps are deleted; crash cleanup is future work).

---

### 10. Testing Strategy (EXPLICIT)

**Unit tests:**
- `PathValidationTests`: name sanitization (all rejection cases: traversal, separators, reserved, empty, length).
- `PathAncestorTests`: component-wise ancestor checks, rejection of lexical prefixes (C:\project vs C:\project-other).
- `CreationSessionTests`: lifecycle transitions, state invariants, sessionId uniqueness.
- `AtomicWriteContractTests`: temp file creation, directory creation, atomic move, cleanup on failure, collision detection.

**Integration tests:**
- `AddStructureToProjectJobTests`: end-to-end write + rollback, TOCTOU races (mock concurrent deletes after validation).
- `SessionRegistryTests`: create/close/switch sessions, event publishing with sessionId + attemptId, stale completion ignoring.
- `DomainRegistrationTests`: coordinator registers structure on main thread before publishing success, cleanup on registration failure.

**Platform-specific security tests (Windows + Unix variants):**
- Symlink/junction substitution (Windows: create link in target folder, then symlink attack during write; expected outcome is unconditional rejection — test asserts the job returns an error, never a partial or misdirected write).
- Lexical-prefix traversal (C:\project-2 vs C:\project, verify ancestor check fails).
- Concurrent same-name creation (two sessions create the same structure name simultaneously; one succeeds, one fails with collision).
- Attacker/race-created destination is never silently overwritten: pre-create destinationPath between the job's collision check and its rename step; expected outcome is the fail-if-exists rename returns a collision error — the pre-existing destination's content is asserted unchanged after the job runs. This is the oracle for the documented best-effort TOCTOU mitigation, not "rejected or handled" (either outcome).

**UI / acceptance tests (manual + future automation):**
- Create from Template → Move to StructureHub → Add to Project → verify POSCAR in ProjectTree.
- Analyze Existing → load POSCAR → edit → Move to StructureHub → Add → verify in ProjectTree.
- Multiple concurrent sessions → close one → verify others unaffected.
- Failed save → edit + retry with same/different name → verify only successful attempt registers.
- ProjectTree folder changed after session creation → StructureHub warns before Add.
- Renderer tab close → NewStructure clears binding safely.

**Deferred to later phases:**
- Crash recovery (startup scan for orphaned directories/temp files).
- Symlink/reparse junctions *in project tree* (rejection of symlinks in path validation is in scope; supporting symlinked folders as project roots is future work).
- Structure validation contract (Phase 0: define what "valid" means — non-zero cell vectors, valid species, coordinate bounds, etc.).
- Renderer toolbar per-pane vs. broadcast action routing (UI-specific; detailed in Phase 2).

---

### 11. Renderer Contract Alignment (NEW)

Ephemeral renderer windows opened during structure creation are **not** registered in domain. They have temporary `RendererWindowId`s linked to sessions, not `StructureId`s. On "Add to Project" success, a new domain registration creates a real `StructureId`, and the app may open a new permanent renderer window if desired (out of scope for now; user manually opens it via ProjectTree). Ephemeral windows close when session ends.

### 12. Centering Preset Labels (NEW)

The Bravais centering preset buttons currently render as bare crystallographic symbols
`P` / `I` / `F` / `C` (`kPresetButtons` in `NewStructureWizardPanel.cpp`). They must spell the
centering out:

| Symbol | Label |
|--------|-------|
| P | `Primitive (P)` |
| I | `Body-centered (I)` |
| F | `Face-centered (F)` |
| C | `Base-centered (C)` |

The symbol is kept in parentheses because it is the standard crystallographic notation and appears
in every Bravais/space-group table the user will cross-reference. For the cubic system these are the
familiar simple-cubic / BCC / FCC cells; the names are deliberately system-independent, since
`BravaisCenteringPreset` applies to every crystal system (a body-centered *tetragonal* cell is not
"BCC"). Long-standing user request, previously unrecorded in any plan.

---

## Key decisions & tradeoffs

| Decision | Rationale | Tradeoff |
|----------|-----------|----------|
| **Session registry at composition root** | Guarantees single source of truth for all active creation sessions. Avoids UI state scattering across NewStructure/StructureHub/Renderer. | Adds App layer responsibility; requires session lifecycle events. |
| **Ephemeral renderer windows (no StructureId)** | Honors domain boundary: renders can preview without committing to domain registry. On success, a new real StructureId is created. | Requires careful cleanup on session close; must not leak resources. Mitigation: publish RendererTabClosed event. |
| **2+1 split layout (exact, not configurable)** | Eliminates ambiguity; 2 windows for comparison (original + modified), 1 for analysis. Resizable separators provide flexibility without adding complexity. | Locks layout; users can't choose 3x1 or other arrangements. Acceptable: can extend later if needed. |
| **Path validation twice (at submission + inside job)** | Defends against the common case (target folder deleted/moved between selection and write). Documented as **best-effort mitigation, not a full TOCTOU guarantee** — a true guarantee needs OS-level no-follow directory-handle operations (open-relative-to-fd, `O_NOFOLLOW`/Windows equivalents), out of scope here. Second check is inside the job, close to the write. | Marginal performance cost. Residual race window between the second check and the write is accepted and documented, not silently claimed away. |
| **Atomic write with temp file + move** | Ensures no partial POSCAR on disk if process crashes or write fails. Rollback is automatic (temp file not renamed). | Requires filesystem support for atomic rename (Windows, Unix both have it). |
| **Settings configurable, not hardcoded** | Different workflows need different close/reset behavior. Users can tune for their use case. | More config surface; settings must be documented and migrated. Mitigated by sensible defaults. |
| **Draft preserved after failure** | Honors lifecycle contract: user can edit and retry without losing work. | Must track which attempt failed and which succeeded (requires attempt IDs in events). |

## Risks / open questions

1. **Atomic database availability:** Create from Scratch mode requires lookup of atomic database (elements, radii, valence). Verify whether PuntukasBridge or a new service provides this. If not, scope changes to "manual element entry" or defer to later phase.

2. ~~PoscarWriter temp-file unification~~ — resolved: elevated to a Phase 0 blocking task (see Implementation phases). No longer an open risk.

3. **Component-wise canonical path on Windows:** Implementation must handle drive letters, relative vs. absolute, UNC paths, symlinks, and junctions correctly. Recommend using `std::filesystem::canonical()` + component-by-component comparison, with symlink rejection at each step.

4. **Settings persistence location:** Settings are in `install/users/default/config/ui_settings.yaml`. Confirm this is the canonical location for UI preferences (not elsewhere in config tree).

5. **Supercell hxkxl UI:** Existing NewStructureWizardPanel has hxkxl entry; verify it's suitable for all 4 modes or if mode-specific tweaks are needed (e.g., "Create from Scratch" might constrain h/k/l ranges).

6. **SessionRegistry cleanup on app exit:** Sessions are ephemeral; on app shutdown, open sessions should be cleaned up and their renderer tabs closed. Confirm App::Shutdown() handles this.

7. ~~Stale completion ignoring~~ — resolved: single-in-flight-attempt constraint (see Data Model) makes concurrent retries impossible by construction; the only remaining case (tab closed mid-attempt) is handled by the explicit Closing-state protocol in Section 7.

## Out of scope

- Changes to rest of application (wavefunctions, bonds, defect analysis, rendering outside structure-creation workflow).
- ProjectTree active-folder mechanism (already implemented; this plan only uses it).
- Export to formats other than POSCAR (POSCAR only for now; extensible later).
- Undo/redo within a single supercell session (single linear redo stack acceptable; session is transient anyway).
- Collaborative editing or multi-user structure sharing.
- Saving to material library from "Analyze Existing" mode (listed as a mode, but save-to-library is deferred).
- Custom renderer layouts beyond 2+1 split (not user-configurable; fixed for MVP).

---

## Implementation phases

1. **Phase 0 (Foundation):**
   - **Blocking, do first:** amend `docs/structure-lifecycle-contracts-2026-09-07.md` to add
     `sessionId`/`attemptId` to the relevant event definitions. No consumer/producer code in later
     phases should be written against the old (un-amended) contract.
   - **Blocking:** redesign `PoscarWriter`/`ScriptRunner` call surface to accept a caller-supplied,
     per-attempt-unique input JSON path, replacing the shared
     `install/users/default/temp/poscar_input.json`. `write_poscar.py`'s `shutil.move` is replaced
     with a fail-if-exists rename primitive (see Section 5).
   - **Blocking:** extend `UIConfig` (and its serializer) with a typed `structure_creation` section
     (`close_renderer_tab_after_save`, `reset_new_structure_after_save`) — the current serializer
     only emits known sections, so an untyped/undeclared subtree would silently fail to persist.
     Add a round-trip (de)serialize test.
   - Add CreationSession model + CreationSessionRegistry
     - Session state machine: Draft → Submitted → Completing → Success/Failed → Closing
     - Don't remove session until all pending attempts complete or timeout (see Closing-state
       protocol in Section 7) — single-in-flight-attempt constraint means at most one attempt to wait for.
   - Add PathValidation module, split by layer:
     - **Core:** generic name sanitization (length, separators, traversal, reserved words) +
       symlink/reparse component inspection (walk each path component, reject if any is a
       symlink/junction — this check does NOT know about project roots).
     - **App (StructureLifecycleCoordinator):** project-root membership via `IsAncestor()` —
       the only layer that knows the set of registered project roots.
     - Explicit symlink rejection happens via the Core component-inspection above, before any
       `canonical()` call — `canonical()` itself never "fails" on a symlink, it resolves it, so
       rejection must happen first (platform-specific: Windows junctions, Unix symlinks).
   - Wire events (SessionCreated, SessionClosed, RendererTabClosed, etc.)
   - Define structure validation contract:
     - Finite, non-degenerate cell vectors (volume > 0)
     - Valid species (in periodic table or element database)
     - Valid atomic coordinates (within/near cell)
     - Renderer preconditions (safe for OpenGL display, no NaN/inf)
   - Verify PuntukasBridge atomic-database support (or scope to manual entry)
   - Verify `PuntukasBridge.LoadStructure()`'s return shape covers Analyze-Existing's needs
     (lattice, species, coordinates); scope a follow-up method if it doesn't
   - **Blocking:** wrap or change the domain-registration entry point (`RegisterAsProjectMember`,
     which currently returns a bare reference with no failure signal) so the coordinator has a
     genuine `Result<StructureId>` API with named failure cases (duplicate id, invalid structure,
     registry unavailable) to call in Section 7's workflow
   - Define AddStructureToProjectJob:
     - Accepts immutable authorized-root snapshot as context
     - Uses staging directory strategy: unique per sessionId + attemptId

2. **Phase 1 (NewStructure):**
   - Mode selector UI: four-tab bar (Create New | From Library | Import File | Analyze Existing) - see Section 2
   - Centering preset buttons relabelled to full names - see Section 12
   - Three view-visibility checkboxes (Unit cell / Supercell / Analysis) - see Section 3
   - Mode-specific forms (each with independent validation, no blocking I/O in Render())
   - Import/Analyze as async jobs (not blocking in Render())
   - "Move to StructureHub" button (publishes event, does NOT create renderer tab yet)
   - Two-way session binding

3. **Phase 2 (Renderer):**
   - **Renderer contract amendment** (same doc as the Phase 0 event-contract change): current
     contract states renderer windows open by `StructureId` only. Add an explicit session-owned
     ephemeral-window path — `RendererWindowId` tagged with `sessionId`, no `StructureId`, distinct
     lifecycle from domain-backed windows — as a first-class amendment, not an app-side workaround.
   - 2+1 split layout (top: 2 windows, bottom: 1, resizable separators)
   - Ephemeral window IDs tagged with sessionId (no domain StructureId)
   - Shared toolbar for all 3 (rotation, zoom, display modes) - ONE vertical + ONE horizontal
     toolbar for the whole tab, never one per view
   - Register `StructureCreationTabsPanel` in `EditorLayer` (it is implemented but unwired, which is
     why the previews currently appear as free-floating windows with a toolbar each)
   - Tab lifecycle: open on SessionReadyForStructureHub, close on RendererTabClosed or user click
   - Tab title reflects mode + hxkxl

4. **Phase 3 (StructureHub):**
   - Session manager UI: list active sessions, highlight active
   - "Add to Project" button (only in StructureHub; triggers validation + job submission)
   - Display session state (Draft, Ready, Submitted, Completing, Success, Failed, Closing)
   - Error display + retry UX for failed sessions

5. **Phase 4 (Integration & Job):**
   - Refactor `StructureLifecycleCoordinator`'s pending-job tracking from JobId-only to
     session/attempt-keyed records; add an explicit `ProjectStructureAddFailed` emission path for
     "JobSystem unavailable" / "Domain unavailable" so those failures are never silent file leaks.
   - AddStructureToProjectJob implementation:
     - Receive immutable authorized-root snapshot from coordinator
     - Second-check validation: IsAncestor + symlink rejection
     - Use staging directory: `<targetDir>/_poscar_staging_<sessionId>_<attemptId>_<uuid>/`
     - Write temp → move staging to final location (atomic)
     - On failure: delete temp; leave staging (quarantine); coordinator handles cleanup
   - Event handling:
     - Job completion (Result<Path>) passed to coordinator
     - Coordinator registers structure (Result<StructureId>) **before** publishing ProjectStructureAdded
     - Stale-completion handling: coordinator checks attemptId against active session state
   - Settings schema + load (close_renderer_tab_after_save, reset_new_structure_after_save, etc.)
   - ProjectTree integration: structure appears after successful write
   - Session cleanup: wait for pending attempts on RendererTabClosed; timeout handling defined

6. **Phase 5 (Testing):**
   - Unit: PathValidation, CreationSession, AtomicWrite
   - Integration: AddStructureToProjectJob, SessionRegistry, TOCTOU scenarios
   - Acceptance: end-to-end workflows (Create → Move → Add → verify in ProjectTree)

---

## Acceptance Criteria

**Data Integrity:**
- [ ] No partial POSCAR on disk if write fails (temp file deleted, POSCAR never created).
- [ ] Destination directory: if write fails, directory is empty or deleted; if registration fails after write, POSCAR is deleted (directory may remain empty).
- [ ] Domain registration is Result-based; failure does not register structure. ProjectStructureAdded NOT published if registration fails.
- [ ] Crash after directory creation: orphan directory may persist; handled in future cleanup phase.
- [ ] Attempt-related events carry sessionId + attemptId, except inbound `AddStructureToProjectRequested` (coordinator assigns attemptId on receipt); stale completions ignored by coordinator.
- [ ] Non-attempt events (SessionCreated, RendererTabClosed) carry sessionId only.

**Security:**
- [ ] Path traversal (../, ..\, ~) rejected at sanitization boundary.
- [ ] Component-wise path-ancestor check prevents lexical tricks (C:\project-2 cannot bypass C:\project).
- [ ] Destination directory validation executed twice: before job submission and inside job before write. Documented as best-effort TOCTOU mitigation, not a full guarantee (no OS-level no-follow handles in scope).
- [ ] Symlinks/junctions in the destination path are rejected by explicit component-by-component inspection performed before canonicalization (not by relying on `canonical()` to fail — it resolves symlinks rather than rejecting them).
- [ ] Note: Symlink-in-project-tree (e.g., project root itself is a symlink) is not in scope for Phase 1; future work.

**Session Management:**
- [ ] Multiple concurrent sessions exist independently; closing one does not affect others.
- [ ] Renderer tabs tagged with sessionId; tab close publishes RendererTabClosed event.
- [ ] Failed adds preserve draft + error; user can edit and retry without losing work.
- [ ] ProjectTree folder selection after session creation results in warning in StructureHub (confirms target before Add).

**Architecture:**
- [ ] NewStructure and StructureHub have no direct coupling (only via session registry + events).
- [ ] CreationSessionRegistry owned by App layer (composition root).
- [ ] PathValidation split: generic checks in Core, project-root authorization in App coordinator.
- [ ] 3-window renderer tab lifecycle managed by session (opens on SessionReadyForStructureHub, closes on RendererTabClosed or user click).

**Behavior:**
- [ ] NewStructure panel shows a four-tab bar: Create New | From Library | Import File | Analyze Existing.
- [ ] Centering preset buttons read `Primitive (P)` / `Body-centered (I)` / `Face-centered (F)` / `Base-centered (C)`, not bare symbols.
- [ ] Three view-visibility checkboxes toggle the Unit cell / Supercell / Analysis views independently; unchecking releases that renderer window, rechecking rebuilds and re-docks it.
- [ ] The creation tab is one renderer window with ONE vertical + ONE horizontal toolbar shared by all three views - no per-view toolbar.
- [ ] Settings for close_renderer_tab_after_save and reset_new_structure_after_save are discoverable + persisted.
- [ ] ProjectTree shows structure immediately after "Add to Project" succeeds.
- [ ] Analyze Existing mode converges on same StructureHub → Add workflow (no parallel save-to-library path).

**Testing:**
- [ ] All unit tests pass (PathValidation, CreationSession, AtomicWrite, SessionRegistry).
- [ ] Integration tests pass (AddStructureToProjectJob, DomainRegistration, TOCTOU races, stale completions).
- [ ] Platform-specific security tests pass (symlink attacks, lexical-prefix bypass, concurrent collision).
- [ ] All existing tests continue to pass (no regression).
- [ ] Manual acceptance tests pass (Template → Add, Analyze → Add, concurrent sessions, failed retry, folder warning).
