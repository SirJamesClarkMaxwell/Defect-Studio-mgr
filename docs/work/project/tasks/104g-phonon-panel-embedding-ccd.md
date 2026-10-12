# Task 104g: embedding + CCD inputs in the Phonons panel, faster eigensolver

## Goal
The Phonons panel (task 104d) exposes the force-constant embedding of the script (task 104f) and
the CCD input generator (task 104e, `scripts/python/examples/ccd_inputs.py`), every new value
persisted. The script diagonalises with scipy's `driver="evd"` instead of numpy's `eigh`
(measured 2.2x faster on this PC: 36x36x1 17.8 s vs 40 s).

## Files to create or change
- `src/ScientificRuntime/Python/PhononAnalysisBridge.cpp` - new request fields -> arguments and
  validation (header already updated - the contract)
- `src/IO/PhononPanelSettingsIO.cpp` - new settings keys (header already updated)
- `src/Presentation/Panels/PhononModesPanel*.cpp` (+ private members in `PhononModesPanel.hpp`)
- `src/Presentation/Panels/ProjectTreePanel.cpp` - "Fonony" RMB submenu: add
  "Ustaw jako katalog FC bulku (embedding)" -> role `"bulk"` (route through the existing
  `ProjectEvents::PhononDirectoryRequested`; `SetDirectory` must accept `"bulk"`)
- `scripts/python/examples/phonon_vibronic_analysis.py` - eigensolver swap only
- a CCD job: reuse `src/ScientificRuntime/Python/PythonScriptJob.{hpp,cpp}` if it can run a script
  with arguments and give back stdout; only if it cannot, add a minimal `CcdInputsBridge`/job next to
  `PhononAnalysisBridge` following the same pattern (no tests required for it beyond building)

## Files that must NOT be touched
- the updated headers' public parts and the tests (`tests/ScientificRuntime/PhononAnalysisBridgeTests.cpp`,
  `tests/IO/PhononPanelSettingsIOTests.cpp`, `scripts/python/tests/*`) - the contract
- `Vendor/`, `premake5.lua`, `install/`, `test-directory/`

## Panel changes
1. **Wejścia** gets an "Embedding (opcjonalnie)" sub-block: katalog FC bulku (field + "..." + hint
   about the RMB), supercell `mult` as three integer inputs (stored as "NxMxK" in
   `embeddingMult`; empty/disabled when the checkbox "Embedding" is off), dcutoff / bcutoff [A] each
   with a checkbox "domyślny". A grey hint: "Czas rośnie ~N^3: 24x24x1 ~5 s, 36x36x1 ~20 s,
   48x48x1 ~2 min na tym PC". The request gets these fields only when embedding is on.
2. Summary line: when the loaded result has `embedding` in summary.json, show
   "embedding: NxMxK, przesunięcie początku (x, y, z) A, dcutoff d A" (read it from summary.json in
   the panel; do not change `PhononAnalysisResult` / `PhononAnalysisIO`).
3. **CCD** collapsing header: uses the GS and ES directories from Wejścia, `ccdOutputDirectory`
   (default `<project>/ccd/<ES dir name>`), `ccdStep`, button **Generuj wejścia CCD** -> runs
   `ccd_inputs.py --gs-dir --es-dir --out-dir --step` off the main thread, shows the JSON result
   (points, dQ) or its error, and a reminder "Przed sbatch: WAVECAR do es/t_+100 i gs/t_+000".
   The script refuses a non-empty output dir - show that message as is.
4. Everything new persists through the save handler, same rules as 104d (save on edit end, nothing
   runs by itself).

## Script change
In `analyse()`, while calling `fc.calculate_modes(...)`, make dephonopy's dense serial path
(`dephonopy/matrix_storage/serial_matrix.py:70`, `np.linalg.eigh(self.data)`) use
`scipy.linalg.eigh(a, driver="evd")` without editing Vendor and without changing numpy globally
for other code (e.g. temporarily replace the module-level `np` name inside `serial_matrix` with a
proxy whose `linalg.eigh` is the scipy call, restored in `finally`). All existing Python tests must
still pass with the same reference numbers.

## Acceptance criteria
1. `DefectStudioTests.exe --gtest_filter=Phonon*` all pass (incl. the new embedded end-to-end test);
   whole suite green.
2. `.venv/Scripts/python.exe -m pytest scripts/python/tests -q` green.
3. `.cpp` files under ~500 lines.
4. Manual (reviewer): embedding 12x12x1 from the panel, CCD generation into an empty folder.

## Constraints
- Building: the sandbox cannot write `build/`; regenerate projects only, check every API you call
  against its header. The reviewer builds.
