# Task 113b — HR method, cutoff and broadening

Implemented on 2026-11-17. Local calculations support `displacements` (default),
`approximate_forces`, `forces`, an optional HR cutoff in Å and optional `sigma_max`
in meV. Server imports accept `sigma_max` and retain the server's HR method/cutoff.
The existing spectra function, publisher, settings serializer and batch queue are reused.

## Behaviour and dephonopy details

- No HR cutoff argument preserves the existing behaviour: no cutoff in the FC cell,
  dephonopy's automatic radius with embedding. An explicit cutoff works without embedding.
  The summary records the radius actually used, including automatic/clamped radii.
- Embedded calculations use the defect sites already returned by dephonopy's FC embedding.
  Without embedding, explicit cutoff calculations use GS positions of atoms tied for the
  largest drift-corrected GS/ES displacement (`isclose`, rtol 1e-5, atol 1e-10 Bohr).
  This deterministic geometric choice does not require identifying substitutions or vacancies.
- Approximate forces call `calculate_force_differences_from_displacements(ES/CONTCAR,
  GS/CONTCAR, bulk_modes)`, then `hr_factors_from_forces`. Bulk FC is accepted without
  `--mult` for this method. The installed helper requires bulk FC in the same cell as GS/ES;
  a mismatched cell gets a clear error. Bulk mode positions are temporarily shifted into the
  defect origin and restored. FC embedding finishes before bulk mode calculation because
  dephonopy mass-weights FC in place during diagonalization.
- For emission, `read_force_differences(init=ES, final=GS@ES,
  check_equal_positions=True)` returns **F_ES − F_GS@ES**. GS@ES must have the same
  geometry, ordered species and masses as ES. Missing bulk FC / GS@ES and nonfinite
  force arrays fail before publishing. Actual force-derived factors come from dephonopy;
  they are not checked against displacement-derived factors. The independent SI-unit
  displacement cross-check is retained for `displacements`.
- `sigma_max >= sigma` is required. Gaussian widths grow linearly from sigma at the lowest
  spectral mode to sigma_max at the highest. The shared spectra function still calls
  dephonopy `gaussian_smoothing(sig_max=...)`. This version computes `sig + slope*x`
  rather than `sig + slope*(x-x_min)`, so the wrapper shifts the frequency/grid origin and
  restores it afterwards. For a single distinct frequency it uses sigma, avoiding division
  by zero. Spectra retain their integral S and existing publication/overwrite protection.
- YAML case inputs round-trip `hrMethod`, optional `hrCutoff` and optional `sigmaMax`.
  Old settings keep displacement/no-cutoff/constant-sigma defaults. Results record
  `hr_method`, `hr_cutoff_A`, `sigma_max_meV` (including null); the C++ loader reads the
  optional `sigmaMaxMeV` result field and accepts absent/null fields from older datasets.
- The case form offers method-specific tooltips and disables methods with missing inputs,
  an auto/manual cutoff and a sigma_max checkbox/DragFloat. Bulk FC selection is available
  without enabling embedding. Requests retain these values when queued or switched between cases.

## Reference calculations

Inputs: `test-directory/sjcm_mgr/PL-spectra/dimer/CH0/R2SCAN/9x9x1`, FC in `dispersion`,
GS in `relax`, ES in `excited/try_1_singlet`; bulk FC:
`test-directory/sjcm_mgr/hBN/bulk/phonons/vasp_661`. Both are present. Default sigma = 6 meV,
gamma = 2 meV. Values below were computed here, not copied from expected values.

| HR method | Cell / cutoff | S_total | Actual HR cutoff [Å] |
|---|---|---:|---:|
| displacements | original 9×9×1 FC, none | 1.4066617489 | null |
| displacements | embedded 9×9×1, auto | 1.4222094401 | 6.0566243270 |
| displacements | embedded 12×12×1, auto | 1.4216871497 | 6.0566243270 |
| displacements | original FC, explicit radius above | 1.4177985779 | 6.0566243270 |
| approximate_forces | original 9×9×1 FC, none | 0.7883808316 | null |
| approximate_forces | embedded 12×12×1, auto | 0.8237285540 | 6.0566243270 |
| approximate_forces | embedded 24×24×1, auto | 0.8760143534 | 6.0566243270 |

The explicit-cutoff calculation in the original FC uses the largest-displacement site,
whereas embedding uses the two identified substitution sites and embedded FC, so these
are different calculations even at the same numerical cutoff. The requested ≈1.422 versus
≈1.407 comparison is reproduced. No real GS@ES force calculation exists for this dimer;
the forces branch is tested with synthetic force differences and real modes instead.

## Validation

- Targeted run: **35 passed, 21 subtests passed**, four existing scientific-library warnings:
  `.venv/Scripts/python.exe -m pytest scripts/python/tests/test_phonon_vibronic_analysis.py scripts/python/tests/test_phonon_server_import.py -q -rP -p no:cacheprovider`.
  Covers method prerequisites, invalid cutoff/broadening, exact Gaussian endpoint widths and
  monotonic growth (2/4/8 meV), equal/single-mode frequencies, summary metadata, real reference
  sizes, emission force argument order, equality with dephonopy's force factors, and server
  CLI reimport with sigma_max while server inputs remain byte/mtime-identical.
- Full Python suite: **96 passed, 5 failed, 21 subtests passed**. Four failures are
  `test_check_include_directions.py::{test_allowed_edge_passes,test_disallowed_edge_detected,
  test_known_exception_skipped,test_non_module_includes_ignored}`: Python 3.13 private
  temporary-directory ACLs cause WinError 5/3 in this sandbox. The fifth is
  `test_ccd_inputs.py::CcdInputsTests::test_dimer_ccd_points`: the existing CCD generator
  rejects missing MAGMOM in a magnetic calculation, followed by the same temporary-directory
  cleanup restriction. Those files are outside 113b and were left unchanged.
- TEMP/TMP were set to `.task-113b-temp` inside this repository for execution. The touched
  vibronic tests use `Path.mkdir` with inherited ACLs, matching the existing dispersion-test
  pattern. Restricted temporary directories from the failed existing tests may require
  cleanup outside the sandbox. Pytest's inaccessible existing cache was disabled.
  Automatic approval review rejected recursive cleanup of `.task-113b-temp` with
  "blocked by policy", so the task's logs/temporary artifacts remain there as well.
- C++ tests were extended for default/optional argument order, local versus server options,
  prerequisites and finite values, a real approximate-forces request without embedding,
  YAML round-trip/defaults/invalid values, optional result loading and queued request snapshots.
  **They were not built or run**: the user owns C++ validation and explicitly prohibited MSBuild.
- `git diff --check` passed. No new `.cpp`/`.hpp` was added by 113b, so project regeneration
  was not required for this task. No commit was made; Vendor/install/build were not edited.

## Scope and concurrent 113e integration

The parallel session's `defectRoots`, `sourceDirectory`, `hidden`, scanner, comparison and
queue refactor changes were preserved. Shared settings and request-building edits were local.
113e's comparison consumes the existing `hrMethod` / `hrCutoffAngstrom` result fields, which
now contain actual local computation provenance as well as server provenance. Its comparison
files are outside the 113b edit list and were not changed here. Broadening is recorded in
summary/settings and available as `sigmaMeV`, `sigmaMaxMeV`, `gammaMeV`; 113e's specified
comparison column contract does not include broadening columns. Adding those columns remains
with the comparison owner rather than editing their files concurrently.

Binding adaptation: the installed `Modes.atoms` property has no setter, and FC diagonalization
mutates FC storage. The temporary position update/restore and embedding-before-diagonalization
ordering preserve the requested behaviour without modifying dephonopy. The Gaussian-origin
adjustment above similarly preserves the requested endpoint widths with the installed API.

## Files changed by 113b

- `scripts/python/examples/phonon_vibronic_analysis.py`
- `scripts/python/examples/phonon_server_import.py`
- `scripts/python/tests/test_phonon_vibronic_analysis.py`
- `scripts/python/tests/test_phonon_server_import.py`
- `src/ScientificRuntime/Python/PhononAnalysisBridge.hpp`
- `src/ScientificRuntime/Python/PhononAnalysisBridge.cpp`
- `src/IO/PhononPanelSettingsIO.hpp`
- `src/IO/PhononPanelSettingsIO.cpp`
- `src/IO/PhononAnalysisIO.cpp`
- `src/Domain/Vibrations/PhononModes.hpp`
- `src/Presentation/Panels/PhononPanelSessionRequests.cpp`
- `src/Presentation/Panels/PhononPanelSessionServer.cpp`
- `src/Presentation/Panels/PhononModesPanelInputs.cpp`
- `tests/ScientificRuntime/PhononAnalysisBridgeTests.cpp`
- `tests/IO/PhononPanelSettingsIOTests.cpp`
- `tests/IO/PhononEmbeddingSettingsTests.cpp`
- `tests/IO/PhononAnalysisIOTests.cpp`
- `tests/Presentation/Panels/PhononBatchTests.cpp`
- `tests/Presentation/Panels/PhononServerSessionTests.cpp`
- `docs/work/project/reports/task-113b-hr-method-cutoff.md`

Repository graph maintenance: `graphify query` had no relevant phonon vocabulary, so scoped
source reads were used. `graphify update .` was invoked as required but exited 1 with
`[graphify watch] Rebuild failed: [WinError 5] Access denied`. It also warned about the
inaccessible test temporary directories. The existing graph was not refreshed; no successful
graph update is claimed.
