# Task 109c: sequential phonon / CCD case queue

Implemented the binding session queue and the Przypadki controls. Checked cases run in list order,
with phonons before CCD regardless of the order of the selected steps. Every queued entry owns a
request built from its case settings at StartBatch. Later input, output, embedding and CCD edits do
not change that request. CCD curvature points to that batch's planned phonon output, frozen at startup.

The session exposes PhononBatchStep, PhononCaseStatus, PhononCaseRun, StartBatch, CancelBatch,
CaseRun, IsBatchRunning and BatchProgress. Unseen cases have the Idle default. Runtime queue state,
statuses and step choices have no persistence schema. Blocked cases receive Failed with the shared
blocker reason and submit no jobs. Progress counts settled case/step entries, including blocked,
skipped dependent and cancelled entries.

Only one analysis is submitted at a time. Update polls completion, commits the result, then submits
the next entry. Single phonon and CCD analysis computations record their originating case and use
the same completion path. Completion attaches the dataset and updates resultDirectory or
selectedCcdDirectory in that case; only an active originating case loads the result into the view.
Inactive phonon results use the existing summary/spectra cache, avoiding another large modes/NPY
allocation. Their attachment and dataset-list revisions remain; no result/view-selection revision
is introduced. CCD input generation also retains its originating attachment and does not replace
another case's result/error readout.

Case activation still clears window arrows and animation and restores case settings, but leaves
computations running. Input-directory edits also leave frozen requests running. Output paths reuse
the existing collision handling with a settings/id overload and the old active-case wrappers.
Attachments reserve queued output directories, following 109b's existing reservation pattern.

Worker failures retain the case's failure reason and continue the queue. An independent CCD step
still runs after a phonon failure; a CCD step requiring that failed phonon output is skipped so it
cannot consume a stale previous result. CancelBatch marks running and queued cases Cancelled,
requests cancellation, clears queued entries, and waits for the current worker to settle before
allowing another computation. Even a completion racing cancellation is not committed. Project
replacement cancels/detaches jobs and clears all batch statuses/progress, following 109a's lifecycle.

The cases table has the requested Status column and failure tooltips. Under it are Kroki checkboxes,
Policz zaznaczone (N), case/step progress and Anuluj. Single and batch submission controls exclude
each other with a disabled reason. The existing single buttons remain.

## Files changed

Existing files:

- `src/Presentation/Panels/PhononPanelSession.hpp`
- `src/Presentation/Panels/PhononPanelSessionJobs.cpp`
- `src/Presentation/Panels/PhononPanelSessionCcdJobs.cpp`
- `src/Presentation/Panels/PhononPanelSessionLifecycle.cpp`
- `src/Presentation/Panels/PhononPanelSessionState.cpp`
- `src/Presentation/Panels/PhononPanelSessionCaseOutputs.cpp`
- `src/Presentation/Panels/PhononPanelSessionCases.cpp`
- `src/Presentation/Panels/PhononModesPanelCases.cpp`
- `src/Presentation/Panels/PhononModesPanelInputs.cpp`
- `src/Presentation/Panels/PhononModesPanelCcd.cpp`

New files:

- `src/Presentation/Panels/PhononPanelSessionBatch.cpp`
- `src/Presentation/Panels/PhononPanelSessionRequests.cpp` (request builders and shared blockers)
- `tests/Presentation/Panels/PhononBatchTests.cpp`
- `docs/work/project/reports/task-109c-phonon-batch.md` (this report)

Existing public APIs and test files/expectations were retained. The shared session header changes
are local blocks. No task edits to IO, Domain, ScientificRuntime, Python scripts, plots, structure
building, Vendor or install. Concurrent 110 / 112e changes and unrelated existing files were left
alone. Generated project files were updated only through the requested regeneration command.

## Tests added

13 GoogleTests in `PhononBatchTests.cpp`:

1. Three cases with a blocked middle case: Done / Failed with reason / Done, strict sequential
   submission despite a two-worker JobSystem, output registration and complete progress.
2. Single phonon completion after switching cases: original attachment/settings, unchanged active
   result, selection and view revision except attachment/list changes.
3. Queued phonon request retains original GS, sigma, embedding multiplier/cutoff and output after edits;
   the active originating case eventually loads the produced result.
4. Mid-run cancellation marks all cases, submits nothing further, and keeps the runner occupied
   until cancellation settles. Repeated cancellation is harmless.
5. Project replacement cancels jobs, clears statuses/progress and discards late completion.
6. Single and batch submissions exclude each other, including CCD inputs and global dispersion.
7. CCD follows phonons and uses the frozen new curvature path, sigma, temperature and output;
   both datasets attach to the inactive originating case.
8. Single CCD analysis survives a case switch and leaves the other case's CCD view untouched.
9. Phonon failure skips dependent CCD and continues with the next case.
10. Shared blockers reject a missing input directory, destructive output and CCD without points/curvature.
11. Non-active batch completion changes only attachment/dataset-list plot revisions.
12. Queued cases sharing inputs receive distinct reserved default output directories.
13. Independent CCD runs after phonon failure while preserving the case's Failed status/reason.

The tests replace execution through a private friend-accessible submission callback. Real immutable
requests, JobSystem scheduling/cancellation, file loaders and main-thread session completion still
run. Gates and WaitUntil come from the existing JobSystem test utilities; no Python is required.

## Verification and anything not done

- Scoped `git diff --check` passed. The 12 task C++ translation units parsed without grammar errors
  using the installed Tree-sitter C++ grammar. The session header has the same constructor-default
  parser limitation as HEAD; no new header grammar error was found. This is not compilation.
- All task `.cpp` files are below 500 lines; the largest is the new test file at 447 lines.
- Regenerated with `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
  Premake completed. Both generated projects include the two new session sources, and the test
  project includes PhononBatchTests.cpp. The wrapper's submodule step printed the known sandbox
  Git `sh.exe` CreateFileMapping / Win32 error 5; project generation still succeeded.
- **MSBuild, GoogleTests and interactive GUI checks were not run**, as requested. Compilation and
  test execution remain for the user; existing-test compatibility has not been execution-verified.
- Graphify was queried first and returned insufficient current phonon context. **Graphify update
  was not run:** the explicit task-only file restriction excludes writes to `graphify-out/`.
- Context7 is not callable in this session. UI API signatures were checked against the official
  [Dear ImGui header](https://github.com/ocornut/imgui/blob/master/imgui.h); no Vendor sources were read.
- No commit was made. All requested implementation work is present in the shared working tree.
