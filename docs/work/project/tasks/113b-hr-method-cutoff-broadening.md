# Task 113b: HR method, cutoff and broadening controls as in the cluster scripts

Plan: `docs/work/project/plans/2026-11-17-phonon-server-results-and-comparison.md`. User 2026-11-17: control the
computation from the app like from the scripts (`P:/PL-spectra/scripts/hr_factors.py`, `lineshape.py`): HR method,
cutoff, σ / σ_max / γ. No GS-at-ES forces exist for the 9×9×1 dimer, so `forces` cannot be checked on real data;
`approximate_forces` (dephonopy: forces from displacements through the bulk FC) works without new DFT and is the
one that can change S with the supercell size.

## Goal

The case inputs offer: HR method (`displacements` | `approximate_forces` | `forces`), HR cutoff (auto or Å, also
without embedding), σ, σ_max (optional: σ grows linearly from σ at the lowest to σ_max at the highest mode, as
`lineshape.py` `SIG_MAX`), γ. Local computations and server imports (113a) honour them where they apply; the
dataset records them; the comparison shows them.

## Design (binding)

1. **Script** `scripts/python/examples/phonon_vibronic_analysis.py`:
   - `--hr-method displacements|approximate_forces|forces` (default displacements), `--hr-cutoff Å` (absent = the
     current behaviour: no cutoff without embedding, dephonopy auto cutoff with embedding), `--sigma-max meV`.
   - HR factors through dephonopy `calc_hr_factors` style calls (`hr_factors_from_displacements` /
     `hr_factors_from_forces` with `calculate_force_differences_from_displacements` for approximate forces - read
     `Vendor/dephonopy/dephonopy/exec_script_hr.py` and reuse, do not re-derive). `approximate_forces` needs the
     bulk FC dir of the case (`--bulk-fc-dir`, also allowed without `--mult` for this purpose); `forces` needs
     `--gs-at-es-dir` (forces of the GS PES at the ES geometry, emission ES -> GS; check dephonopy's
     `read_force_differences(init, final)` argument order and document it). Missing input -> clear error.
   - Keep the independent S_k cross-check for `displacements`; for the force methods compare against dephonopy's
     own S_k only.
   - The cutoff without embedding: dephonopy `defect_sites` from the GS/ES difference (the atoms with the largest
     displacement, or `find_defect_sites` when a bulk structure is available) - document the choice.
   - σ_max: the shared spectra function (extracted in 113a) gets `sigma_max`; dephonopy `gaussian_smoothing`
     already supports `sig_max` - reuse.
   - summary: `hr_method`, `hr_cutoff_A` (the value actually used, also when auto), `sigma_max_meV` (null).
   - `phonon_server_import.py` gets `--sigma-max` too (method/cutoff there are fixed by the server result).
2. **C++**: `PhononAnalysisRequest` + `BuildPhononAnalysisArguments` / `BuildPhononServerImportArguments`
   (`--hr-method`, `--hr-cutoff`, `--sigma-max` appended in that order before `--out-dir`, only when set /
   not default) - update the argument tests; `PhononCaseSettings` (`hrMethod`, `hrCutoffAngstrom`,
   `sigmaMaxMeV`), yaml keys `hrMethod`, `hrCutoff`, `sigmaMax` (optional), round-trip test; loader reads
   `sigma_max_meV`; UI in the case inputs: method combo with a tooltip per method (what it needs; `forces` disabled
   with the reason when „GS w geometrii ES” is empty; `approximate_forces` disabled with the reason when no bulk FC),
   cutoff: checkbox „auto” + DragFloat Å, σ_max: checkbox + DragFloat. Comparison columns from 113e show the values.
3. **Check** (write the numbers into the report): dimer 9×9×1 (`test-directory/sjcm_mgr/PL-spectra/dimer/CH0/R2SCAN/9x9x1`,
   bulk FC `test-directory/sjcm_mgr/hBN/bulk/phonons/vasp_661` if present) - `displacements` with cutoff auto vs none
   (expected ≈1.422 vs 1.407), `approximate_forces` at the FC cell and embedded 12×12×1 and 24×24×1.

## Files to create or change

- `scripts/python/examples/phonon_vibronic_analysis.py`, `phonon_server_import.py`, their tests
- `src/ScientificRuntime/Python/PhononAnalysisBridge.{hpp,cpp}` + tests, `src/IO/PhononPanelSettingsIO*`,
  `src/IO/PhononAnalysisIO.cpp`, `src/Domain/Vibrations/PhononModes.hpp` (one field), session request building,
  `PhononModesPanelInputs.cpp`
- `docs/work/project/reports/task-113b-hr-method-cutoff.md`

## Files that must NOT be touched

- `Vendor/` (dephonopy used, not patched), `install/`, `build/`, renderer, energetics, `src/IO/DataTableIO.*`,
  `src/Domain/Vibrations/ExperimentalSpectrum.*`; nothing outside the repo is written

## Acceptance criteria

1. pytest green (new tests: argument validation per method, σ_max changes S(ℏω) width monotonically, summary fields).
2. C++ argument and settings tests updated and passing (I build and run).
3. Report with the numbers of the check in design 3.

## Constraints

- STYLE RULE: keep existing formatting of edited files. `.cpp` under ~500 lines.
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only (pytest allowed: `.venv/Scripts/python.exe -m pytest scripts/python/tests -q`).
  Do NOT run MSBuild. Do not commit.
- If a binding detail conflicts with existing code, keep the user-visible behaviour, document the deviation and continue.
