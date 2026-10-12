# Task 104b: phonon result - domain helpers and directory reader

## Goal
The app can load the result directory written by `scripts/python/examples/phonon_vibronic_analysis.py`
(task 104a, schema `ds.phonon_vibronic.v1`) into a `PhononAnalysisResult`, and compute from it the
arrows of one mode with a participation threshold, the number of atoms that carry a given fraction
of the mode, and animation frames. No UI in this task - the panel (104d) builds on these.

## Files to create or change
- `src/Domain/Vibrations/PhononModes.cpp` (new) - implements `BuildModeArrows`,
  `AtomsCarryingFraction`, `AnimatedPositions` declared in `src/Domain/Vibrations/PhononModes.hpp`
- `src/IO/PhononAnalysisIO.cpp` (new) - implements `PhononAnalysisIO::Load` declared in
  `src/IO/PhononAnalysisIO.hpp`, including a minimal NPY v1/v2 reader (file-local, anonymous
  namespace) for little-endian float32, C-order arrays

## Files that must NOT be touched
- `src/Domain/Vibrations/PhononModes.hpp`, `src/IO/PhononAnalysisIO.hpp` (the contract - if a
  signature is wrong, stop and say so)
- `tests/Domain/PhononModesTests.cpp`, `tests/IO/PhononAnalysisIOTests.cpp` (the contract)
- everything else under `src/`, `tests/`, `scripts/`, `Vendor/`, `premake5.lua`

## Acceptance criteria
1. `DefectStudioTests.exe --gtest_filter=PhononModesTests.*:PhononAnalysisIOTests.*` all pass.
2. The rest of the suite is unaffected.
3. Loading the real result directory of the reference case (run the 104a script on
   `test-directory/sjcm_mgr/PL-spectra/dimer/CH0/R2SCAN/9x9x1`, see the 104a task file for the
   command) succeeds: 162 atoms, 486 modes, sTotal ~1.407.
4. Each `.cpp` under ~500 lines.

## Constraints
- `Domain` must not include anything from `IO`, `Renderer`, `Presentation`, `App`
  (AGENTS.md layer rules). `PhononModes.cpp` uses only the standard library and glm.
- JSON via `nlohmann/json` (already used in `src/IO/MaterialLibraryIO.cpp`). Text files through
  `TextFileIO::Load` where convenient; `.npy` read as binary.
- `nan` / `null` handling: `zpl_eV` entries may be `null` -> empty optional; a `pl.tsv` column that
  is entirely `nan` -> empty vector. `modes.tsv` `is_translation` is `0`/`1`.
- `structure.json` `cell_A` rows are lattice vectors; store them as the COLUMNS of `cellAngstrom`
  (glm is column-major: `cellAngstrom[1]` is b).
- No exceptions escape `Load` (nlohmann throws - catch and turn into `outError`). `outResult` is
  only assigned on success.
- Physical displacement for arrows/animation: `u_i = e_ki / sqrt(m_i)` with `e_ki` from
  `eigenvectors`, `m_i` from `massesAmu`.
- Build: regenerate projects (`DS_TOOLSET=msc-v143`, `scripts/Windows/GenerateProjects.bat`) after
  adding the `.cpp` files. If MSBuild cannot write into `build/` from the sandbox, say so and stop;
  the reviewer builds.
