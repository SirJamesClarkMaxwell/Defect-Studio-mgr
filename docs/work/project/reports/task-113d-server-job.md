# Task 113d — server job folder generator

Implemented 2026-11-17. No commit, MSBuild, cluster connection or job submission.

## Behaviour

`scripts/python/examples/server_job.py` takes the user templates directory, defect/bulk FC,
GS/ES, optional energy/force directories, HR method/cutoffs, broadening, ZPL/formula and
inclusive n range. It copies all template assets, including nested non-Python files, and
changes known module-level CONFIG assignments. Unknown names and unavailable optional
embedding cutoffs retain their template values. Multiline unknown assignments, comments,
line endings and non-CONFIG code are retained except for the explicit compatibility edits below.
The series runner's individual or tuple-unpacked size bounds are supported.

The generator reuses `phonon_vibronic_analysis.origin_shift`, `read_toten` and the existing
ZPL evaluator. It validates GS/ES against the defect FC species, masses and cell, subtracts
the same origin shift from both structures, wraps them, and writes only job-owned
`inputs/{gs,es}/CONTCAR_aligned` files. Input calculations and source user assets are read only.

Generated script input paths are relative to `HERE`. Cross-drive inputs receive portable
`../cluster-inputs/<role>` locations; `README.txt` and `job.json` give the source-to-relative
mapping that the user must reproduce on the cluster. Large FC datasets are referenced,
not duplicated. `SYSTEM` is the job folder, with the existing `embedded/`,
`spectral-function/` and `lineshape/` result layout, ready for the existing server rescan/import.
README contains the single embedding command, existing series-runner command, HR submission
and lineshape command, with instructions to wait between stages.

Generation accepts only a new or empty destination. Exclusive creation and a job-local lock
prevent overwriting existing files; ordinary write failures remove only files this invocation
created. `job.json`, written last, records configured names, compatibility edits, relocation
paths, ZPL, origin shift and size range. Abrupt process termination can leave an incomplete
folder; choose a fresh folder or inspect/remove that incomplete output before retrying.
There is no second publish/backup system: unlike replaceable analysis datasets, an existing
server job folder is never replaced, and no sibling staging folder is written.

The case inputs now contain a **Serwer** section with n bounds, template and destination
selection, a disabled reason for missing inputs, preparation/cancellation/progress,
case-specific completion messages, relative result paths and an explorer button. HR,
embedding cutoffs, broadening and ZPL use the existing case controls. The tooltip gives
only the measured 10.6 GB at n=10 / 30 GB at n=50 with 36 MPI ranks.
Generator controls are transient per-case UI state, not a new settings schema.

`ServerJobBridge` reuses `PhononAnalysisRequest`, the existing argument builder, script
resolver, subprocess runner and structured errors. `ServerJob` uses the existing JobSystem
with cancellation and progress. Session completion is polled on the main thread, routed to
the originating case, and cancelled/cleared on project change/destruction. No batch queue,
dataset scanner, spectra calculation or automatic submission system was added.

The automatic destination is `<case source>/defectstudio/server_job_<timestamp>`, falling
back under the project when filesystem permissions reject the source tree. The default
selector does not write a probe into calculation trees. If Windows ACLs reject a seemingly
writable default, the generator retries the project fallback. Explicit user destinations
never silently fall back.

## Binding deviations required by the real user assets

The assets in `install/users/default/scripts/server_templates/` were read in place and
never edited or copied into tracked fixtures or documentation. Tests contain only original,
tiny fake templates. The following narrowly scoped changes are applied to generated copies,
and are listed separately from CONFIG replacements in each job's manifest:

- The series runner has a non-CONFIG output-root constant tied to a particular calculation.
  It is redirected to the generated job's embedding directory, in addition to setting its
  size bounds. Keeping only the two size assignments would break restart/completion checks.
- Defect FC construction outside CONFIG assumes a particular child directory of SYSTEM.
  Its argument is changed to the selected FC's relative path so arbitrary selected FC
  directories work while outputs remain in the job folder.
- The embedding call outside CONFIG uses a fixed primitive structure. Generated copies
  use the selected bulk FC primitive cell and apply the local origin shift to the in-memory
  defect FC geometry before embedding. The reference-size HR branch receives the same shift.
  Explicit dcutoff/bcutoff values are passed when the template call does not already supply them.
- The old reference-size comparison in the HR loader follows the defect-cell size instead
  of retaining its original fixed size. The user's other scientific/template logic remains
  user supplied, including defect-site selection and scheduler/environment settings.
- The current HR template does not pass bulk modes for `approximate_forces`; the generated
  call supplies them. For `forces`, INIT/FINAL refer to ES and GS-at-ES directories containing
  force outputs, and the generated call shifts the force geometry in memory. Aligned relaxed
  GS/ES files are still written for both methods.

These edits exceed the CONFIG-only rule because preserving it literally would produce
non-working jobs for the selected paths/methods. No original template source text is
embedded in the generator. Unsupported absolute Windows paths left in a Python user asset
produce an explicit error instead of a misleading portable job.

## Files changed by this task

New:

- `scripts/python/examples/server_job.py`
- `scripts/python/tests/test_server_job.py`
- `src/ScientificRuntime/Python/ServerJobBridge.hpp`
- `src/ScientificRuntime/Python/ServerJobBridge.cpp`
- `src/ScientificRuntime/Python/ServerJob.cpp`
- `src/Presentation/Panels/PhononPanelSessionServerJob.cpp`
- `src/Presentation/Panels/PhononModesPanelServerJob.cpp`
- `tests/ScientificRuntime/ServerJobBridgeTests.cpp`
- `tests/Presentation/Panels/PhononServerJobSessionTests.cpp`
- this report.

Small integration edits:

- `src/Presentation/Panels/PhononPanelSession.hpp`: controls, job slot, entry points, busy state.
- `src/Presentation/Panels/PhononPanelSessionLifecycle.cpp`: main-thread polling.
- `src/Presentation/Panels/PhononPanelSessionJobs.cpp`: cancellation through the existing lifecycle.
- `src/Presentation/Panels/PhononModesPanel.hpp`: draw declaration.
- `src/Presentation/Panels/PhononModesPanelInputs.cpp`: section call.

Concurrent 113f2 changes in shared files were retained. This task did not edit settings IO,
experimental spectra, comparison, renderer, energetics, Vendor, HANDOFF or other session files.
All new C++ implementation files are below 500 lines.

## Verification

- Final focused pytest run: **19 passed**, one dependency deprecation warning.
  Coverage: generated Python compilation with `py_compile`, configured values, relative
  paths, unknown/default preservation, byte-preserved non-Python assets, unchanged source
  assets, tuple bounds, validation, missing templates, overlap/overwrite refusal, failure
  cleanup, destination races, cross-drive mapping, permission fallback, scientific
  compatibility edits, and reuse of the real local origin alignment function.
- Full `scripts/python/tests` run: **111 passed, 5 failed, 21 subtests passed** at collection
  time (before four additional focused cases were added). The failures were the existing
  CCD reference test and four include-direction tests, which encountered Windows temp-tree
  access/cleanup errors. Those out-of-scope tests were not edited.
- Real assets + local reference FC/GS/ES: generated successfully; all **5 Python scripts
  compiled**. A generated n=9 HR job and lineshape script ran successfully: **S=1.4217,
  DW=0.2413, effective phonon energy 146.1 meV**. Smoke outputs stayed inside the repository.
  Automatic approval review rejected recursive cleanup with "blocked by policy", so the
  artifacts were preserved under ignored `build/task-113d-smoke/` instead. Moving the folders
  makes their local relative input references stale; these are diagnostic artifacts, not
  deliverable cluster folders. No MPI embedding or scheduler command was executed.
- Added **8 C++ tests** covering arguments/units, JSON/schema/errors, request validation,
  job failure/cancellation, a fixture-dependent successful job with original fake templates,
  active-case inputs, per-case/project state, and disabled reasons. **Not built or run**,
  as requested; the user runs C++ tests.
- Ran the required PowerShell project-generation command with `DS_TOOLSET=msc-v143`.
  Both generated application/test projects include the new sources and tests. Premake
  completed successfully; the submodule helper emitted a Git shell CreateFileMapping
  permission warning and premake emitted existing deprecation warnings.
- Ran required `graphify update .` (AST only, no API cost). It warned about inaccessible
  temporary directories belonging to the concurrent 113f2 session, then failed with
  `Rebuild failed: [WinError 5] Access denied`. The graph refresh could not be completed;
  no graph permissions or other session artifacts were changed to work around it.

Remaining checks: C++ build/test execution, live ImGui interaction, MPI/scalapack embedding,
cluster environment/scheduler behaviour, and numerical validation of the generated
`forces`/`approximate_forces` jobs. User assets remain responsible for scientific assumptions
outside the explicitly adapted legacy paths/calls; the generator does not rewrite arbitrary
scientific code.
