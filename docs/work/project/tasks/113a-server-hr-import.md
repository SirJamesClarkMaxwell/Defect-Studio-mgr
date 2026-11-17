# Task 113a: import Huang-Rhys results computed on the cluster (read in place)

Plan: `docs/work/project/plans/2026-11-17-phonon-server-results-and-comparison.md`. User 2026-11-17: the app must
read finished server calculations ("bardzo ważne"); derived datasets go to the project folder, the server tree is
never written; eigenvectors are NOT loaded here (113g fetches one mode on demand).

## Goal

In the `Fonony` window the user picks a folder on the mounted cluster drive (P:), the app lists every dephonopy
HR result below it (all supercells n×n×1 and variants such as `displacements_cutoff4A`), and imports the checked
ones into the active case as ordinary phonon datasets (`ds.phonon_vibronic.v1`). S(ℏω) and PL are recomputed from
the server's S_k sticks with the case's σ/γ/ZPL settings, so they compare 1:1 with locally computed datasets.

## Server layout (real example: `P:/PL-spectra/dimer/CH0/R2SCAN/9x9x1`)

```
<system>/spectral-function/12x12x1/displacements/
    freq_mev.dat     3N-3 rows, meV (dephonopy drops the 3 translations; negative = imaginary)
    hr_factors.dat   3N-3 rows, S_k
    irreps.dat       3N-3 rows, labels (A1, B2, ..., ???)
    fc_displ.dat     3N-3 rows, signed Delta Q_k in dephonopy units
    hr_factors.log   contains "init_path: <cluster path>", "final_path: <cluster path>",
                     "cutoff radius (auto|user): <x> A", "Stot: <x>"
<system>/embedded/12x12x1/
    POSCAR           the n×n×1 supercell (positions, cell, species)
    iprs.dat         3N rows, dephonopy IPR (= app n_eff)
    freq.dat, irreps.dat, defect.yaml, modes.h5 (large; NOT read in 113a)
<system>/relax/{OUTCAR, CONTCAR, CONTCAR_aligned}            ground state
<system>/excited/try_1_singlet/{OUTCAR, CONTCAR_aligned}     excited state
```
For n = 9 (the defect cell itself) `embedded/9x9x1/` also exists; if `embedded/<size>/` is missing, use
`<system>/dispersion/SPOSCAR` or `POSCAR` for the structure and leave n_eff unknown.
Cluster paths in the log look like `/scratch/franciszek/sjcm_mgr/PL-spectra/dimer/.../relax/CONTCAR_aligned`.

## Design (binding)

1. **C++ scanner** `src/IO/PhononServerResultsIO.{hpp,cpp}`: header is written (contract), implement
   `FindServerHrResults` exactly as documented there. Tests `tests/IO/PhononServerResultsIOTests.cpp` are written.
2. **Python** new `scripts/python/examples/phonon_server_import.py`
   `--hr-dir D [--zpl-exp eV] [--zpl-formula F] [--label L] --sigma meV --gamma meV --out-dir O`:
   - reads the four `.dat` files + log; maps each cluster path onto the local drive by the **longest path suffix**
     that exists below some ancestor of `--hr-dir` (e.g. `.../sjcm_mgr/PL-spectra/dimer/.../relax/CONTCAR_aligned`
     -> `P:/PL-spectra/dimer/.../relax/CONTCAR_aligned`); unmapped -> that state is unknown (not an error);
   - GS dir = directory of the mapped final_path, ES dir = directory of init_path (emission ES -> GS, as in
     `hr_factors.py`); computed ZPL = E_es - E_gs from their OUTCARs via `read_toten` when both exist, else null;
     the formula ZPL uses the same `zpl_formula` helpers as the local script (variables that need missing dirs ->
     a clear error); ΔQ and max |ΔR| from the two mapped CONTCARs exactly like the local script
     (`read_atomic_displacements`, mass-weighted drift removed), null when unmapped;
   - modes: 3 translation rows first (freq from `embedded/<size>/freq.dat` converted to meV if present, else 0;
     `is_translation` 1, S 0), then the 3N-3 server rows; `q_k = sign(fc_displ_k) * sqrt(2 ħ S_k / ω_k)` in
     amu^½ Å (consistent with the local formula in `mode_factors`; 0 for ω <= 0); n_eff from `iprs.dat` (3N rows)
     else `nan`; beta = n_atoms / n_eff (nan when unknown);
   - spectra: move the S(ℏω) + PL block of `analyse()` (grid choice, `gaussian_smoothing`, normalisation to
     S_total, `calc_luminescence` per ZPL, common photon grid, max-1 normalisation) into one function in
     `phonon_vibronic_analysis.py` and call it from both scripts - no copy;
   - output via the existing `publish()`; `write_result()` must skip `eigenvectors.npy`/`participation.npy`
     when the result has no eigenvectors. summary.json: same schema `ds.phonon_vibronic.v1` plus
     `"source": "server"`, `"has_eigenvectors": false`, `"supercell": [a,b,c]`, `"hr_method"`, `"hr_cutoff_A"`
     (number or null; from the log), `"server": {"hr_dir", "hr_factors_mtime"}`, `"input_paths"` with `hr_dir`,
     `gs_dir`, `es_dir` (null when unmapped), `"embedding"`: `{"mult": [a,b,c]}` when a,b,c != the FC cell, and
     `"approximation"` naming dephonopy `calc_hr_factors` + the method. The local script additionally writes
     `"source": "local"`, `"has_eigenvectors": true`, `"hr_method": "displacements"`, `"hr_cutoff_A"` (null), and
     `"supercell"` (mult or null). Default label: `<system dir name> <AxBxC> <variant>`.
3. **Loader** `src/IO/PhononAnalysisIO.cpp`: read the new fields (all optional - old datasets load unchanged with
   the defaults in `PhononAnalysisResult`); when `has_eigenvectors` is false, skip both `.npy` files and leave the
   vectors empty; accept `nan` in `n_eff`/`beta` of modes.tsv and `null` for `max_displacement_A`,
   `delta_Q_amu05_A`, `zpl_eV.computed` (store NaN / nullopt; every place that shows them prints „—”).
4. **No vectors, no crash**: `BuildModeArrows`, `AtomsCarryingFraction`, `AnimatedPositions`, the participation
   column, the vibration field / animation start, and window binding must handle `!hasEigenvectors` (empty
   result / unchanged positions / disabled control). Disabled buttons say why: „Wynik z serwera nie ma wektorów
   własnych (będą wczytywane na żądanie - zadanie 113g).”
5. **Bridge**: `PhononAnalysisRequest::serverHrDirectory` and `BuildPhononServerImportArguments` (contract in the
   header, tests written). `Run` uses `phonon_server_import.py` when `serverHrDirectory` is non-empty, validating
   only: hr dir exists (error `python.phonon.request_invalid`, message names the path), output non-empty,
   sigma/gamma > 0. Same JSON result line as the local script.
6. **UI + session** (`Fonony` window, next to the case inputs): button „Wczytaj wyniki z serwera…” -> folder
   picker -> scan off the main thread (a job like the dataset scan; cancellable; the drive is slow) -> popup listing
   hits (system relative to the project or shortened, supercell, variant, cutoff) with checkboxes, all checked,
   „Zaznacz wszystkie/Odznacz”, then „Wczytaj zaznaczone (N)”. Each hit becomes one request with the active case's
   σ/γ/experimental ZPL/formula, output `<project>/defectstudio/phonons/server/<system dir name>/<AxBxC>/<variant>`
   (unique per hit; never next to the server data), queued through the **existing batch queue** of
   `PhononPanelSession` (sequential, status per entry, cancel) and attached to the active case when done. The
   import never switches the active case. Imported datasets show a small „serwer” marker (tooltip: server hr dir).
   Re-import of the same hit overwrites its directory (as recomputing does).
7. Datasets list / comparison: nothing new beyond showing the marker; existing comparison works because the
   dataset is a normal `ds.phonon_vibronic.v1`.

## Files to create or change

- new `src/IO/PhononServerResultsIO.cpp` (header exists), new `scripts/python/examples/phonon_server_import.py`,
  new `scripts/python/tests/test_phonon_server_import.py` (synthetic fixture: a tiny fake server tree with 2-4
  atoms, OUTCARs with TOTEN lines, CONTCAR_aligned, a log with fake `/scratch/.../sjcm_mgr/...` paths)
- `scripts/python/examples/phonon_vibronic_analysis.py` (extract spectra function, new summary fields, optional npy)
- `src/IO/PhononAnalysisIO.cpp`, `src/Domain/Vibrations/PhononModes.cpp` (guards; header fields already added)
- `src/ScientificRuntime/Python/PhononAnalysisBridge.cpp`
- `src/Presentation/Panels/PhononPanelSession*.cpp`, `PhononPanelSession.hpp` (private members/methods only),
  `PhononModesPanel*.cpp` for the button, popup, marker and disabled reasons
- tests: add a loader test (`tests/IO/` - find the PhononAnalysisIO tests) for a `has_eigenvectors: false` dataset
  without `.npy`, with nan n_eff and null ΔQ; old-dataset test still passes

## Files that must NOT be touched

- `src/IO/PhononServerResultsIO.hpp`, the new/changed test expectations in `tests/IO/PhononServerResultsIOTests.cpp`
  and `tests/ScientificRuntime/PhononAnalysisBridgeTests.cpp`, the public fields added to `PhononAnalysisResult`
- `Vendor/` (dephonopy is used, not patched), `install/`, `build/`, renderer, structure-building and energetics files
- `docs/` except `docs/work/project/reports/task-113a-server-hr-import.md` (write it: files, tests, deviations)
- nothing on P: or any other drive outside the repo is written

## Acceptance criteria

1. `.venv/Scripts/python.exe -m pytest scripts/python/tests -q`: new tests pass (S_total = sum of server S_k for
   ω > 0, ZPL from TOTENs, unmapped path -> null ZPL/ΔQ and still a dataset, no `.npy` written, local-script tests
   unchanged); nothing that passed before fails.
2. Python spectra of the local script are byte-for-byte unchanged in the existing tests (only moved).
3. C++: `PhononServerResultsIOTests`, the new bridge tests and the new loader test pass; existing tests pass.
4. Manual check I will run (do not run it yourself unless P: is reachable; never write there):
   `phonon_server_import.py --hr-dir P:/PL-spectra/dimer/CH0/R2SCAN/9x9x1/spectral-function/12x12x1/displacements
   --sigma 2 --gamma 0.5 --out-dir <scratch>` gives S_total 1.4215 ± 5e-4, DW 0.2412 ± 5e-4.
5. Report written.

## Constraints

- Layers: IO has no UI; Presentation adapters without ImGui stay so; only the main thread commits session state.
- `.cpp` files under ~500 lines (`PhononPanelSession*.cpp` are split by topic - add `PhononPanelSessionServer.cpp`
  rather than growing a big one).
- UI rules (memory of the user): paths shown relative to the project (full path in tooltip), labels left/above,
  tooltips on every control, disabled buttons say why, one-row highlight.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
- If a binding detail conflicts with the existing code, keep the user-visible behaviour, document the deviation in
  the report and continue - do not stop.
