# Task 113g: one mode's eigenvector on demand for server results

Plan: `docs/work/project/plans/2026-11-17-phonon-server-results-and-comparison.md`. User 2026-11-17: eigenvectors for
server results on demand, one mode (50×50×1 = 15000 modes, `modes.h5` of GB size). Builds on 113a (server datasets
have `has_eigenvectors: false`, summary `server.hr_dir`; the embedded dir is `<system>/embedded/<AxBxC>/`).

## Goal

Selecting a mode of a server dataset and pressing „Wczytaj wektor modu” (or starting the animation / arrows) reads
that single mode from `modes.h5`, and arrows, participation and the „Animacja drgań” work for it exactly as for
local results. Nothing else of the file is read.

## Design (binding)

1. **Script** new `scripts/python/examples/phonon_mode_vector.py --embedded-dir D --mode K --out-dir O`: open
   `D/modes.h5` with h5py (inspect the real layout first: `P:/PL-spectra/dimer/CH0/R2SCAN/9x9x1/embedded/12x12x1/modes.h5`
   is 3.3 MB - read only; dephonopy `Modes.from_hdf5` / `save_data_to_dir` shows how it is written), read only mode
   K (a slice, never the full matrix), the structure from `D/POSCAR`, masses via ASE; write `mode.json`
   (`schema: ds.phonon_mode_vector.v1`, mode index, frequency meV, n_atoms) + `eigenvector.npy` float32 (atoms, 3),
   normalised mass-weighted like `eigenvectors.npy` of the local script (state the convention check in the
   report: participation sums to 1). Index K is the app's mode index (translations included, as in modes.tsv of
   the server import) - map it to the h5 row and test the mapping on a fixture.
2. **C++**: a reader for that output (IO), a job (like `PhononModeJob`), cache per (dataset, mode) in the session
   (LRU, keep the last 8 modes); when loaded, the result's eigenvector/participation rows for that mode are filled
   (keep `PhononAnalysisResult` vectors sized for all modes only if memory allows: for > 2000 atoms store the
   fetched modes in a side map and make `BuildModeArrows` / `AnimatedPositions` / `VibrationField` read through one
   accessor - change these functions minimally and keep their tests green). The embedded dir comes from the summary
   (`server.hr_dir` -> system -> `embedded/<supercell>`); missing `modes.h5` -> disabled button with the reason.
3. The supercell structure for the renderer window: „Otwórz superkomórkę” opens `embedded/<AxBxC>/POSCAR` (read in
   place) so animation has a window to drive; window binding as for local results.

## Files to create or change

- new script + pytest (fixture h5 written by the test with h5py in dephonopy's layout), new IO reader + job + tests,
  `src/Domain/Vibrations/PhononModes.{hpp,cpp}` and `VibrationField.cpp` (accessor only), session + panel files
- `docs/work/project/reports/task-113g-server-mode.md`

## Files that must NOT be touched

- `Vendor/`, `install/`, `build/`, renderer internals, energetics; nothing outside the repo is written

## Acceptance criteria

1. pytest: slice read equals the full read on a fixture; index mapping with translations; participation sums to 1.
2. C++ tests: reader, accessor used by arrows/animation, cache eviction.
3. Manual (I run): dimer 12×12×1 server dataset, A1 mode near 200 meV animates in the 288-atom window.
4. Existing tests pass. Report with the click path.

## Constraints

- Layers as usual; `.cpp` under ~500 lines; STYLE RULE; regenerate projects after adding files; PowerShell
  built-ins (pytest allowed); do NOT run MSBuild; do not commit; document deviations and continue.
