# Task 113a — server Huang–Rhys import

Implemented the cluster HR importer, scanner, bridge routing, optional-vector loader, and Fonony import flow.
The supplied public contracts and test expectations were left unchanged. No commit or MSBuild run was made.

## Behaviour

- The server picker uses the existing `AnalysisDatasetScanJob` and scan slot, off the main thread with cancellation.
  Its popup shows system, supercell, variant, cutoff and initially checked selections.
- Selected hits become ordinary `PhononAnalysisRequest`s in the existing sequential batch queue. Each hit has a
  status and error tooltip; cancellation covers both the running request and queued requests. Completion attaches
  successful results to the originating case without activating that case.
- Output is under `<project>/defectstudio/phonons/server/<system>/<size>/<variant>`. Different systems with the
  same last directory name receive numeric suffixes to prevent collisions. Re-import finds the previous directory
  by HR provenance, including equivalent P: and UNC paths, and replaces it through the existing staged `publish()`.
- The importer reads the four HR tables, log, optional embedded POSCAR/frequencies/IPRs, mapped aligned geometries
  and OUTCARs. It does not read eigenvectors, participation arrays or `modes.h5`, and never writes into the server
  system tree. Missing mapped states or OUTCARs leave the corresponding values unknown. Missing formula variables
  produce the existing formula helper's explicit error.
- Local and imported results use one spectra function and one writer/publisher. Positive-frequency sticks determine
  the imported S total and emission spectra; raw finite server sticks remain in the mode table. Translation rows
  are prepended, signed q values use the local SI conversion, and unknown participation is written as TSV `nan`.
- Old datasets retain their existing defaults. Server datasets load without either NPY file. Unknown displacement
  and participation values display as “—”; mode arrows, fields, animation and window binding reject unavailable
  vectors. The UI explains task 113g. Dataset and comparison tables show the server marker and HR-directory tooltip.
- The existing dataset scanner now recognises nested results below `defectstudio`, including server imports.

## Files added

- `src/IO/PhononServerResultsIO.cpp`
- `scripts/python/examples/phonon_server_import.py`
- `scripts/python/tests/test_phonon_server_import.py`
- `src/Presentation/Panels/PhononPanelSessionServer.cpp`
- `src/Presentation/Panels/PhononModesPanelServer.cpp`
- `tests/Presentation/Panels/PhononServerSessionTests.cpp`
- This report.

## Files modified

- `scripts/python/examples/phonon_vibronic_analysis.py`: shared spectra and units, local provenance, optional NPY output.
- `src/Domain/Vibrations/PhononModes.cpp` and `VibrationField.cpp`: explicit no-vector guards.
- `src/IO/PhononAnalysisIO.cpp`: optional provenance, null/NaN support, optional NPY loading.
- `src/IO/AnalysisDatasetIO.cpp`: nested dataset discovery.
- `src/ScientificRuntime/Python/PhononAnalysisBridge.cpp`: contract argument builder, import validation and script selection.
- `src/Presentation/Panels/PhononPanelSession.hpp`: private scan/import state, methods and test access only.
- `src/Presentation/Panels/PhononPanelSession.cpp`: no-vector binding checks.
- `src/Presentation/Panels/PhononPanelSessionBatch.cpp`: per-hit status through the existing queue.
- `src/Presentation/Panels/PhononPanelSessionDatasets.cpp`: extend the existing scan job and main-thread polling.
- `src/Presentation/Panels/PhononPanelSessionEmbedding.cpp`: server embedding metadata display.
- `src/Presentation/Panels/PhononPanelSessionJobs.cpp`: attach server results on successful completion.
- `src/Presentation/Panels/PhononPanelSessionState.cpp`: clear server state on project changes.
- `src/Presentation/Panels/PhononModesPanel.hpp` and `PhononModesPanelInputs.cpp`: import UI entry point.
- `src/Presentation/Panels/PhononModesPanelDatasets.cpp` and `PhononModesPanelComparison.cpp`: server markers, unknown values.
- `src/Presentation/Panels/PhononModesPanelView.cpp`: unknown readouts and unavailable-vector controls.
- `src/Presentation/Panels/PhononPlotData.cpp`: unknown participation export and finite-first mode sorting.
- `tests/IO/PhononAnalysisIOTests.cpp`: server result without NPY files and with null/NaN values.
- `tests/IO/AnalysisDatasetIOTests.cpp`: nested server dataset discovery.

Unrelated working-tree changes, including the supplied headers/tests and concurrent tasks, were preserved.
No renderer, structure-building, energetics, install or Vendor source files were edited for this task.

## Verification

1. New Python tests: **7 passed**, including 3 validation subtests. They cover TOTEN ZPL, drift-corrected displacement,
   S total, signed q consistency with local `mode_factors`, shared spectra normalization, unknown states, missing
   formula inputs, embedded frequency units/IPRs, fallback structure, longest-suffix mapping, publication without
   NPY files, re-import and unchanged server file bytes/mtimes.
2. Full Python suite with a process-local Windows temporary-directory workaround: **94 passed, 1 failed**,
   with 10 subtests passed. The sole failure is the existing
   `TestCheckIncludeDirections.test_disallowed_edge_detected`: it applies string membership to a `Violation`
   object. The same failure occurred before adding the new tests (87 passed, 1 failed). It was not modified.
3. The normal pytest invocation was attempted first. Python 3.13's mode-0700 temporary-directory ACL prevents
   this sandbox from accessing those directories, causing permission errors in existing tests: 65 passed,
   13 failed, 11 errors. The validation workaround changes `os.mkdir` mode 0700 to inherited mode 0777 only
   inside the pytest process; it does not modify application, test or interpreter files. TEMP/TMP stayed in the repo.
4. Compared the original HEAD script and the extracted implementation on the local 162-atom reference case,
   with computed, spin-purified, experimental and formula ZPLs. The emitted numerical tables are byte-for-byte
   identical: spectral function 297,566 bytes, PL 3,833,279 bytes (`%.12g`, tab-separated).
5. Ran the real P: example read-only, writing output inside the repo. Current file results:
   S_total **1.4220915380949255**, DW **0.2412089911470206**, computed ZPL **3.4520160800000212 eV**,
   delta Q **0.33435715835948027 amu^0.5 Å**, maximum displacement **0.06320904417683598 Å**,
   288 atoms / 864 modes, stable. No NPY files were published.
6. Ran the requested generation command with `DS_TOOLSET=msc-v143`. Premake completed and regenerated both
   projects and filters. Verified that both projects include the new production CPP files and the test project
   includes `PhononServerSessionTests.cpp`. The wrapper also emitted a Git for Windows shared-memory
   `Win32 error 5` diagnostic; the subsequent generation itself completed.
7. `git diff --check` passed. The include-direction checker reports three existing edges in unmodified
   `Events/ProjectEvents.hpp` and `Core/Domain/StructureLifecycleEvents.hpp`; no task file introduced a violation.
8. C++ compilation, C++ test execution and interactive UI verification are **not run**, as requested. Added C++
   coverage for the loader, nested discovery, all no-vector consumers, unknown-value sorting/export, re-import collision handling, case switching,
   sequential import and cancellation. The supplied scanner and bridge tests were not edited.

## Binding details and limitations

- The real embedded `freq.dat` declares `frequencies (mev)`. The importer honours the header and converts other
  dephonopy frequency units through `Units.convert_frequencies`; headerless files use the library's default meV.
  It does not assume every saved frequency file is in atomic units.
- Existing case settings store experimental numeric ZPL through the numeric formula preset, rather than a
  separate experimental-ZPL field. Imports preserve that existing UI behaviour. Explicit bridge requests still
  support `experimentalZplEv` and emit the contract's `--zpl-exp` argument.
- Collision suffixes extend the specified output layout only when different HR sources would otherwise overwrite
  each other. Existing matching sources reuse their directory.
- A fallback SPOSCAR/POSCAR must have the atom count implied by the HR table's 3N rows. Inconsistent input is
  rejected rather than inventing geometry for a larger supercell.
- The current P: file's direct positive-frequency S sum is slightly outside the task's `1.4215 ± 0.0005` range
  (upper bound 1.4220). DW is inside its requested range. The importer preserves the file-derived sum instead
  of adjusting data to a stale reference number.
- `graphify update .` was attempted; its rebuild failed with `WinError 5` (access denied). The existing graph
  remains intact but stale. A second attempt after moving validation artifacts under ignored `.codex/` failed with
  the same error; its log is `.codex/task-113a-artifacts/graphify-update.txt`.
- Validation logs and the generated real-server dataset are retained under ignored `.codex/task-113a-artifacts/`.
  This also contains empty temporary directories whose Python-created ACLs prevented sandbox cleanup.
