# Task 108c: bulk phonon mode at q (`phonon_mode.py`), bulk per case, animation of the bulk mode

Plan: `docs/work/project/plans/2026-10-14-vibration-animation.md` (slice 108c). Builds on 108a (`VibrationField`,
"Animacja drgań") and 108b (ΔQ source, per-case animation persistence, source-dependent target window). User
decisions: 2026-10-14 (non-commensurate q = fragment with a warning + the smallest N×M×K hint with „Użyj”;
degenerate mixing slider from the start) and 2026-10-20 (animation state per case; the bulk is a dataset shared by
many cases, each case references its bulk; nothing auto-starts on load).

## Goal

The user picks q (reduced coordinates) and a branch of the case's bulk, presses „Policz mod”, and animates that bulk
phonon in a bulk supercell window, with the same controls as a defect mode (amplitude, period, phase, arrows, θ mixing
of a degenerate partner). The dispersion shown and the bulk mode follow the active case: two cases with different
bulks show different dispersions after a case switch, two cases with the same bulk FC share the scanned datasets.

## Design (binding)

1. **Dispersion writes q** (`scripts/python/examples/phonon_dispersion.py`): `write_result` also writes
   `qpoints.tsv` (header `qx\tqy\tqz`, one row per row of `dispersion.tsv`, same order, reduced coordinates of the
   phonopy PRIMITIVE reciprocal basis exactly as `get_band_structure_dict()["qpoints"]` gives them, `%.12g`).
   `summary.json` gains `"qpoints_file": "qpoints.tsv"` and `"q_basis": "primitive_reciprocal_reduced"`.
   `dispersion.tsv` is NOT changed (old readers keep working).
2. **New script** `scripts/python/examples/phonon_mode.py` (reuse `publish` from `phonon_vibronic_analysis.py` and
   the FC loading of `phonon_dispersion.analyse`: `ForceConstants(fc_dir)`, `ensure_symmetry()` unless
   `--no-symmetrize`, `fc.Phonopy(unit_cell=fc.atoms)`):
   - args: `--fc-dir`, `--q qx qy qz` (same basis as `qpoints.tsv`), `--band` (zero-based branch at that q,
     ascending frequency), `--supercell auto|NxMxK` (diagonal multiples of the phonopy UNIT cell), `--in-plane`
     (z multiple forced to 1), `--max-multiple` (default 24), `--degenerate-tol-mev` (default 0.5), `--out-dir`,
     `--self-test`.
   - Compute with phonopy's modulation API (`run_modulations` / `get_modulations_and_supercell`), NOT a hand-written
     phase: it applies e_κ · exp(i 2π q·(R + τ_κ)) / √m_κ exactly once. VERIFY in the installed phonopy source which
     basis its modulation q uses (primitive vs unit-cell reciprocal) and convert from the primitive basis if needed;
     write the conversion and the source line you checked into the report.
   - `auto` supercell: the smallest diagonal N×M×K (each ≤ `--max-multiple`, K = 1 with `--in-plane`) with
     q_unit_i · N_i ∈ ℤ (tolerance 1e-6), q_unit = q converted to the unit-cell reciprocal basis. If none exists:
     use the smallest N×M×K whose fragment shows at least one wavelength along each in-plane direction where q ≠ 0,
     capped by `--max-multiple`, and set `commensurate: false`. An explicit NxMxK that is not commensurate is
     computed anyway with `commensurate: false`. `suggested_supercell` = the smallest commensurate one or null.
   - Degenerate partner: the other branch at the same q with |Δω| ≤ tolerance (closest one); its field is written too.
   - Output (via `publish`): `POSCAR` of the supercell, `vibration.json`:
     ```json
     {"schema": "ds.phonon_mode.v1", "label": "q=(1/3, 1/3, 0) gałąź 5", "frequency_meV": 165.2,
      "q": [qx,qy,qz], "q_basis": "primitive_reciprocal_reduced", "band": 5, "n_bands": 6,
      "supercell": [N,M,K], "commensurate": true, "suggested_supercell": [N,M,K] | null,
      "symbols": [...], "masses_amu": [...], "cell_angstrom": [[...],[...],[...]],
      "positions_angstrom": [[x,y,z], ...],
      "real_angstrom": [[...], ...], "imag_angstrom": [[...], ...],
      "partner": null | {"band": 4, "frequency_meV": 165.2, "real_angstrom": [...], "imag_angstrom": [...]},
      "input_paths": {"fc_dir": "..."}, "inputs_relative": {...}}
     ```
     Fields normalised so the largest |u| over the cycle is 1 Å (the partner with the SAME factor as the main band
     would get after its own normalisation - i.e. normalise each to max 1 Å independently). Also write a minimal
     `summary.json` with `"schema": "ds.phonon_mode.v1"`, `label`, `input_paths` so the dataset scan finds it.
   - `--self-test`: a tiny synthetic Phonopy (e.g. 2-atom chain or simple cubic with hand-made FC, no files):
     (a) Γ acoustic branch = rigid translation; (b) q = (1/2,0,0) with `auto` gives N = 2 and the field is periodic
     under the supercell translation; (c) q = (1/3,0,0) gives N = 3; (d) q = (0.123,0,0) gives
     `commensurate: false`, `suggested_supercell: null` with `--max-multiple 24`; (e) mass division happens once
     (two masses, compare with e/√m by hand at Γ optical).
   - Real-data check for the report (do not commit outputs): bulk hBN FC in
     `test-directory/sjcm_mgr/hBN/bulk/phonons/vasp_661` at K (1/3, 1/3, 0) with `--in-plane`: N×M×K = 3×3×1,
     the two degenerate branches at K report a partner.
3. **IO** `src/IO/PhononModeIO.{hpp,cpp}` (new): `Load(directory, PhononModeField &out, std::string &error)` for
   `vibration.json`; `PhononModeField` lives in new `src/Domain/Vibrations/BulkMode.hpp` (plain data: label,
   frequencyMeV, q, band, bandCount, supercell, commensurate, optional suggestedSupercell, symbols, masses, cell,
   positions, real, imag, optional partner {band, frequencyMeV, real, imag}). Validation: equal per-atom lengths,
   finite numbers, schema match. `PhononDispersionIO::Load` reads the optional `qpoints.tsv` into a new
   `std::vector<glm::dvec3> qpoints` on `PhononDispersion` (absent -> empty, no error; row count mismatch -> error).
   `AnalysisDatasetKind::BulkMode` for schema `ds.phonon_mode.v1` (kind dir name `bulk_mode`).
4. **Domain** (`VibrationField.{hpp,cpp}`): `VibrationField BulkModeField(const PhononModeField &, double
   mixAngleRadians = 0.0)`: real/imag = cos θ·main + sin θ·partner (partner absent -> θ ignored), equilibrium =
   positions, uniform participation computed as |u|² share (sums to 1), label `bulk · <label> · ℏω X meV`.
   Plus `std::optional<std::array<int,3>> CommensurateSupercell(glm::dvec3 qUnitReduced, int maxMultiple, bool
   inPlane)` only if the C++ side needs it - otherwise skip it (the script decides; YAGNI).
5. **Job** `src/ScientificRuntime/Python/PhononModeJob.{hpp,cpp}` like `PhononDispersionJob` (publishes the result
   path/error after execution). Session keeps ONE pending mode job; a new submission cancels the pending one
   (newest request wins - 108d relies on this).
6. **Bulk per case** (persistence, `PhononPanelSettingsIO*`): move the dispersion selection into the case:
   `PhononCaseSettings` gains `Path selectedDispersionDirectory;` (the base copy keeps working through the existing
   base<->active-case sync), `glm::dvec3 bulkQ{0,0,0}`, `int bulkBand = 0`, `std::string bulkSupercell = "auto"`,
   `Path selectedBulkModeDirectory`. Case YAML: `datasets` of the case already exist - add `dispersion` and
   `bulkMode` keys in the case map (read the old global `datasets.dispersion` as the default for every case
   without its own value - migration, no warning) and `view.bulkQ: [x,y,z]`, `view.bulkBand`,
   `view.bulkSupercell`. `animationSource` (108b) gains `bulk_q`. On case switch / project load: load the case's
   dispersion and bulk mode if set; if the case has none but the dataset scan found a `Dispersion` dataset whose
   input `fc_dir` equals the case's `bulkForceConstantDirectory`, select it (loading data is fine, computing is
   not). Computing a dispersion or a mode stores the output in the ACTIVE case only.
7. **UI** (`PhononModesPanelView.cpp` "Animacja drgań", new file if the view passes ~500 lines):
   source `mod bulku w q` enabled when the case has a bulk FC directory (else disabled: „Ustaw katalog FC bulku w
   przypadku.”). Controls: `q` (3 DragFloat, reduced, label „q (zredukowane, baza prymitywna)”), `Gałąź`
   (InputInt, 0..n_bands-1 when a dispersion is loaded), `Superkomórka` (text, `auto` or `NxMxK`, validated, tooltip),
   `Policz mod` (disabled with the reason: no bulk FC / job running / invalid supercell), progress + cancel, error
   line. Result line: `ℏω = X meV, q = (…), gałąź n, N×M×K`; if `commensurate == false`: yellow warning „Ruch nie
   domyka się okresowo w tej superkomórce (fragment fali).” + if `suggested_supercell` exists a button
   „Użyj N×M×K” that sets the supercell and recomputes; if not: „Brak współmiernej superkomórki ≤ 24”. Degenerate
   partner -> the existing θ slider („Zdegenerowany z gałęzią n”). `Otwórz superkomórkę bulku` opens the mode's
   POSCAR as a window bound to the session (target window for this source, same mechanism as 108b's GS window).
   Switching source stops the animation (rest positions restored). The bulk mode result is NOT a defect window:
   defect-mode arrows/animation must not touch the bulk window and vice versa.
8. **Datasets table**: bulk-mode datasets appear with their kind (marker like the other kinds); opening one sets
   the case's `selectedBulkModeDirectory` and its q/band/supercell fields.

## Files to create or change

- `scripts/python/examples/phonon_dispersion.py`, new `scripts/python/examples/phonon_mode.py`
- new `src/Domain/Vibrations/BulkMode.hpp`; `src/Domain/Vibrations/VibrationField.{hpp,cpp}`
- new `src/IO/PhononModeIO.{hpp,cpp}`; `src/IO/PhononDispersionIO.{hpp,cpp}`; `src/IO/AnalysisDatasetIO.{hpp,cpp}`;
  `src/IO/PhononPanelSettingsIO.{hpp,cpp}`, `src/IO/PhononPanelSettingsIOCases.cpp`
- new `src/ScientificRuntime/Python/PhononModeJob.{hpp,cpp}`
- `src/Presentation/Panels/PhononPanelSession.hpp` + session `.cpp` files (new `PhononPanelSessionBulkMode.cpp`
  for the job/load/window code), `PhononModesPanel.cpp` (animation section without a defect result),
  `PhononModesPanelView.cpp` (or a new `PhononModesPanelAnimation.cpp`),
  `PhononModesPanelDatasets.cpp`, `PhononModesPanelDispersion*.cpp` only for the per-case selection
- tests: `tests/Domain/Vibrations/VibrationFieldTests.cpp`, new `tests/IO/PhononModeIOTests.cpp`, the dispersion IO
  tests (search `PhononDispersionIO` in `tests/`), `tests/IO/PhononPanelSettingsIOTests.cpp`,
  `tests/Presentation/Panels/PhononViewsTests.cpp` / the cases tests (search `PhononCasesTests`)

## Files that must NOT be touched

- `ccd_analysis.py`, `ccd_inputs.py`, `phonon_vibronic_analysis.py` (import `publish`, do not edit), renderer code,
  structure-building files, `Vendor/`, `install/`, `build/`; `docs/` except
  `docs/work/project/reports/task-108c-bulk-mode.md`

## Acceptance criteria

1. `.venv/Scripts/python.exe scripts/python/examples/phonon_mode.py --self-test` passes (a)-(e); the dispersion
   script's existing tests pass and a dispersion output has `qpoints.tsv` with the same row count.
2. Real-data check reported (K point 3×3×1, degenerate partner) - numbers in the report.
3. IO: synthetic `vibration.json` roundtrip; partner absent/present; malformed -> error; `qpoints.tsv` absent ->
   empty, mismatched rows -> error; dataset scan recognises `ds.phonon_mode.v1`.
4. Domain: `BulkModeField` at θ = 0 equals the main field, θ = 90° the partner; participation sums to 1.
5. Persistence: two cases with different bulk FC keep their own dispersion/bulk-mode selection and q/band/supercell
   across save/load and case switch; an old file with only the global `datasets.dispersion` gives every case that
   dispersion; after load nothing is computing and nothing animates.
6. Session: a second „Policz mod” cancels the first; source `bulk_q` disabled with the reason without bulk FC;
   animating the bulk window then Stop restores its rest positions exactly; a defect window is untouched.
7. Existing tests pass. Report: files, tests, the phonopy basis check, real-data numbers, anything not done.

## Constraints

- Python computes, C++ draws; no new Python dependency (phonopy/dephonopy are already in the runtime).
- Layer rules: Domain has no IO/UI includes; IO does not build fields; the session (Presentation) composes.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
