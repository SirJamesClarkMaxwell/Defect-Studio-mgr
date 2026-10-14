# Task 109c: compute many phonon cases in one go (sequential queue)

Plan: `docs/work/project/plans/2026-10-13-phonon-workspace.md`. Builds on 109b (cases, `liczyć` checkbox,
`CaseOutputDirectory`). Decision (recorded in the plan): batch = phonon analysis (with the case's embedding variant)
and/or CCD analysis as selectable steps, run **sequentially** (an embedded 36x36x1 run takes GBs of RAM).

## Goal

The user ticks `liczyć` on several cases, picks the steps, clicks `Policz zaznaczone`, and can keep working: each
case shows its status, the active case is never switched by a finishing job, results appear as datasets attached
to their own case, and the queue can be cancelled.

## Design (binding)

1. **Queue in the session** (pattern: `DefectEnergeticsSessionFnv.cpp` FNV queue - copy its shape, not its code):
   ```cpp
   enum class PhononBatchStep { Phonons, CcdAnalysis };
   enum class PhononCaseStatus { Idle, Queued, Running, Done, Failed, Cancelled };
   struct PhononCaseRun { PhononCaseStatus status = PhononCaseStatus::Idle; std::string message; Path output; };
   void StartBatch(std::vector<std::string> caseIds, std::vector<PhononBatchStep> steps); // ids in list order
   void CancelBatch();                                 // running job cancelled, queued -> Cancelled
   [[nodiscard]] const PhononCaseRun &CaseRun(const std::string &caseId) const; // Idle default
   [[nodiscard]] bool IsBatchRunning() const;
   [[nodiscard]] std::pair<std::size_t, std::size_t> BatchProgress() const; // done, total (case x step)
   ```
   Each queue entry is an IMMUTABLE request built when the batch starts from that case's settings (later edits of
   the case do not change a queued request). Before queueing, a per-case blocker check reuses the same rules as the
   single `Policz` button (missing FC/GS/ES dirs, output containing an input, CCD without points...) - a blocked
   case gets `Failed` with the reason and is skipped; nothing is submitted for it.
2. **One job at a time**: `Update()` polls the running job; on completion: attach the output dataset to the ORIGINATING
   case (`AttachDataset`), add it to the dataset list, set its `resultDirectory` / `selectedCcdDirectory` in that
   case's stored settings; ONLY if that case is the active one, load the result into the view (same path as today's
   single computation). Then submit the next request. Failure: `Failed` + message, continue with the next.
   CCD step of a case runs after its phonon step (it may use the new phonon result when the case uses phonon curvature).
3. **Switching cases never cancels work** (found in review 2026-10-14: `ActivateCase` calls `cancelCaseJobs()`,
   so changing the active case silently kills a running computation). The single `Policz` / `Policz CCD` jobs also
   record their originating case id and complete through the same path as a batch entry (attach to that case,
   load into the view only if it is active). `ActivateCase` stops the animation and unbinds the window as now, but
   leaves jobs running. Test: start a single computation in case A, activate B, complete -> dataset attached to A,
   B's view untouched.
4. **Single `Policz` stays** and is disabled with a reason while a batch runs (one runner at a time); the batch
   button is disabled with a reason while a single computation runs.
5. **UI** (`Fonony`, `Przypadki` section): under the table a row `Kroki:` checkboxes `fonony` / `analiza CCD`,
   button `Policz zaznaczone (N)`, while running a progress bar `k / n` + `Anuluj`; a `Status` column in the cases
   table (`-`, `w kolejce`, `liczy…`, `gotowe`, `błąd` with the message in a tooltip, `anulowano`).
6. Project switch / `SetPersistedState` cancels the batch (same as 109a for single jobs). Batch state is not persisted.

Requests for a non-active case are built from that case's stored `PhononCaseSettings`: generalise
`CaseOutputDirectory`, `computationOutputPath`, `ccdAnalysisOutputPath` and the blocker checks
(`ComputationBlocker` in `PhononModesPanelInputs.cpp`, `ccdAnalysisBlocker`) to take a case's settings + id instead
of reading the active ones (keep the existing no-argument forms as thin wrappers for the active case). Move
`ComputationBlocker` into the session so the panel and the batch share it.

## Files to create or change

- `src/Presentation/Panels/PhononPanelSession.hpp`, new `PhononPanelSessionBatch.cpp`, the session job/lifecycle files
- the main view files: `PhononModesPanelCases.cpp` (Przypadki), `PhononModesPanelInputs.cpp` and `PhononModesPanelCcd.cpp` (blockers moved to the session), `PhononPanelSessionCaseOutputs.cpp`, `PhononPanelSessionCases.cpp` (ActivateCase)
- tests: new `tests/Presentation/Panels/PhononBatchTests.cpp` (use the existing JobSystem test utils / gates; a fake
  or stubbed job is fine if real Python is not needed - search how other session tests stub jobs)

## Files that must NOT be touched

- `src/IO/*` (no schema change), `src/Domain/*`, `src/ScientificRuntime/*`, `scripts/python/*`
- structure-building files (another task), plots files (task 110 may run)
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-109c-phonon-batch.md`

## Acceptance criteria (GoogleTests)

1. Three cases, one blocked: statuses end `Done`, `Failed` (reason), `Done`; jobs ran strictly one after another
   (never two submitted at once).
2. A completed job for a non-active case attaches its dataset to that case and does NOT change the active case,
   its loaded result or `PlotRevision()` of the active view (except the dataset list revision if any).
3. Editing a queued case's inputs after `StartBatch` does not change its request.
4. `CancelBatch` mid-run: running -> `Cancelled`, queued -> `Cancelled`, nothing else submitted.
5. Existing tests pass; report lists files, tests, anything not done.

## Constraints

- Only the main thread commits results (in `Update`). UI conventions as in 109b.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
