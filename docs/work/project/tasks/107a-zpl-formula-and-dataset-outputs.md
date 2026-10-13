# Task 107a: ZPL formula + self-describing dataset outputs (Python side)

User decision 2026-10-13: results are datasets stored next to the calculations
(`<common input dir>/defectstudio/<kind>/`), auto-discovered later by the app (107b); ZPL is a
formula over named energies with presets. This task does the script side only.

## Goal
1. **`scripts/python/examples/zpl_formula.py`** (new, stdlib only):
   - `VARIABLES = ("E_gs", "E_es", "E_T", "E_es@gs", "E_gs@es")` - final TOTEN (eV) of the ground
     state, excited state, triplet (at the excited geometry), excited state at the ground geometry,
     ground state at the excited geometry. In formula text `@` is written as is
     (`E_es@gs`); internally rewrite `@` to `_at_` before parsing.
   - `PRESETS = {"dscf": "E_es - E_gs", "spin_purified": "2*E_es - E_T - E_gs"}`.
   - `evaluate(formula, energies) -> float`: `ast.parse(mode="eval")`; allowed nodes only:
     Expression, BinOp(Add, Sub, Mult, Div), UnaryOp(UAdd, USub), Constant(int|float, not bool),
     Name from VARIABLES. Anything else -> `ValueError("Niedozwolony element we wzorze ZPL: ...")`.
     Unknown name -> ValueError listing the allowed names. Name known but its energy is None (the
     directory was not given) -> ValueError("Wzór używa E_T, ale nie podano katalogu trypletu").
     Plain number formulas (e.g. "1.945") are allowed = experimental ZPL. Result must be finite and > 0.
   - `used_variables(formula) -> list[str]` (in VARIABLES order).
   - A `__main__` self-check is not needed; tests go in the test file below.
2. **`phonon_vibronic_analysis.py`**:
   - new optional args `--zpl-formula TEXT`, `--gs-at-es-dir`, `--es-at-gs-dir` (dirs only read for
     TOTEN via the existing `read_toten`; only read when the formula uses them).
   - With a formula: `zpls["formula"] = evaluate(...)`; summary gains
     `"zpl_formula": {"text": ..., "variables_eV": {name: value for used names}, "value_eV": ...}`
     (`null` without a formula). PL is computed for it like the other ZPLs.
   - `pl.tsv` gains a 5th column `intensity_formula` (all nan without a formula). Header becomes
     `photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\tintensity_formula`.
   - summary gains `"inputs_relative"`: each input path relative to the published out dir
     (`os.path.relpath`, `/` separators; on a different drive keep the absolute path). Computed in
     `publish()` from the final out dir, so it is right wherever the result lands. `input_paths`
     (absolute) stays.
   - summary gains `"label"`: `--label TEXT` or, by default, the excited-state directory name plus
     `" (emb NxMxK)"` when embedded.
3. **`publish()`** (shared by all three scripts): an output INSIDE an input directory is now allowed
   (e.g. `<fc dir>/defectstudio/dispersion`); still forbidden: out dir == an input, or an input
   inside the out dir (publish replaces the out dir wholesale and would delete it). Update the
   existing test that asserted the old rule.
4. **`ccd_analysis.py`**: `--zpl-formula` (+ `--gs-at-es-dir`/`--es-at-gs-dir` already exist in
   point mode - reuse them as E_gs@es / E_es@gs sources). When given, it is the first ZPL candidate
   (before `--zpl`); summary `zpl_eV.formula` + the same `zpl_formula` block; `inputs_relative` and
   `label` as above (label default: es dir name, or ccd dir name).
5. **`phonon_dispersion.py`**: `inputs_relative` and `label` (default: fc dir name).
6. **C++ reader** `src/IO/PhononAnalysisIO.cpp`: accept both the old 4-column and the new 5-column
   `pl.tsv` header; new `PhononAnalysisResult` fields `plFormula` (vector, empty when all nan),
   `zplEv.formula` (optional), `zplFormulaText` (string, empty when null), `label` (string, may be
   empty for old results). Add them to `src/Domain/Vibrations/PhononModes.hpp` next to the existing
   ones. Old result directories must still load.

## Files to create or change
- create `scripts/python/examples/zpl_formula.py`, `scripts/python/tests/test_zpl_formula.py`
- change `scripts/python/examples/{phonon_vibronic_analysis,ccd_analysis,phonon_dispersion}.py`
  and their tests in `scripts/python/tests/`
- change `src/IO/PhononAnalysisIO.cpp`, `src/Domain/Vibrations/PhononModes.hpp`,
  `tests/IO/PhononAnalysisIOTests.cpp` (new-header case + old-header case still passes)

## Files that must NOT be touched
- `src/Presentation/`, `src/ScientificRuntime/` (another task is editing Presentation right now),
  `Vendor/`, `install/`, `test-directory/` (read only - tests may read the V2CBCN dimer data that
  already exists there, as the current tests do), `premake5.lua`

## Acceptance criteria
1. `python -m pytest scripts/python/tests -q` (repo venv / the python the repo tests use): all new
   tests pass, nothing that passed before fails (known pre-existing failure:
   `test_check_include_directions::test_disallowed_edge_detected`).
2. `test_zpl_formula.py`: presets give 2*E_es - E_T - E_gs and E_es - E_gs; `"E_es@gs - E_gs"`
   works; `"__import__('os')"`, `"E_es**2"`, `"abs(E_es)"`, `"E_x"` raise ValueError; missing E_T
   raises with the Polish message; `"1.945"` returns 1.945; negative result raises.
3. Dimer reference (point mode in `test_ccd_analysis.py`): `--zpl-formula "2*E_es - E_T - E_gs"`
   gives the same 3.7551 eV as the spin-purified candidate today.
4. C++ test for the 5-column pl.tsv; reviewer builds Debug+Release (sandbox cannot build).
5. Report what you could not verify.

## Constraints
- Python: stdlib + what the scripts already import. No `eval`/`exec` anywhere.
- PowerShell built-ins only (Git Bash tools crash in the sandbox). Do not edit UTF-8 files with
  `Get-Content`/`Set-Content` (mojibake) - use the file edit tool.
- `Domain` must not include IO/UI headers.
