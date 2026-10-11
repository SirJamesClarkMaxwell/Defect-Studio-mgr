# Task 86: FNV engine - step 1 in Python (pymatgen), step 2 in C++

Branch `task/86-fnv-engine` off `task/85-density-centre`. Plan:
`docs/work/project/plans/2026-10-06-fnv-review.md` ("Plateau: półautomatycznie", "Test zgodności
pymatgen-analysis-defects 2026.3.20 vs sxdefectalign"). The N-panel / plot UI is the next task (87);
this one is the engine and its tests only.

## Goal

Given a defect LOCPOT, a bulk LOCPOT, q, epsilon and the defect centre, the app computes the FNV
correction: Python (pymatgen-analysis-defects, pinned 2026.3.20 in `.venv`) returns E_lat and the
three planar-average profiles; C++ proposes the plateau window per axis, computes the alignment
C, the dV-vs-V_model slope and E_corr = E_lat + q C, instantly and repeatably for any window. On
the BSc-thesis GeN q = -1 data it reproduces sxdefectalign's E_corr 0.229874 eV within 1 meV.

## The contract (already written - do not change signatures or test expectations)

- `src/Domain/Defects/FnvCorrection.hpp` - profiles, conventions, plateau proposal, alignment, combination.
- `src/ScientificRuntime/Python/FnvCorrectionBridge.hpp` - request, bridge, job.
- Tests: `tests/Domain/Defects/FnvCorrectionTests.cpp` (uses the sxdefectalign reference output in
  `tests/ScientificRuntime/fixtures/fnv/GeN_q-1/vline-eV-a{0,1,2}.dat`),
  `tests/ScientificRuntime/FnvCorrectionBridgeTests.cpp` (the golden case is env-gated by
  `DS_FNV_GOLDEN_DIR`; I run it).

## Do

1. `src/Domain/Defects/FnvCorrection.cpp` - implement the header. `ProposeFnvAlignment` is a
   port of `propose_window` and `model_slope` in `scripts/python/fnv_sxdefectalign.py` (read it):
   - defect position = position of max |V_model|; L = axis.length; periodic distance from the
     defect `dist = |((x - x0 + L/2) mod L) - L/2|`;
   - widths `minimumWidth, minimumWidth + dx, ...` while `<= L/2 + 1e-9` (dx = position[1] -
     position[0]); a point is in the window when `dist >= L/2 - width/2`; keep the LAST width
     whose population std of `dV - V_model` is `<= tolerance`; none -> minimum width, flat false;
   - window.centre = `fmod(x0 + L/2, L)`; alignment = mean residual in the window;
   - slope = least-squares slope of dV on V_model over points with `dist > kFnvSlopeCoreRadius`
     (numpy `polyfit(v_model, dv, 1)[0]`); 0 when V_model is constant there.
   `AlignFnvInWindow`: same numbers for a given window (inside = periodic distance to
   `window.centre` <= width/2 + 1e-12). `FnvModelPotential`: copy for Pymatgen; add
   `(dielectric - 1) * modelPotentialMean` to every point for Sxdefectalign. `CombineFnvCorrection`:
   mean / max-min of the used axes' alignments, `correction = latticeEnergy + charge * alignment`.
   Domain only: std + glm, no allocation in inner loops beyond the result vectors.
2. `scripts/python/examples/fnv_freysoldt_profiles.py` - usage
   `<defect LOCPOT> <bulk LOCPOT> <q> <epsilon> <fx,fy,fz> <encut_eV>`. Model it on
   `scripts/python/examples/vasp_density_grid_load.py` (argument handling, one JSON line on stdout,
   errors as a message on stderr + exit 1) and on `test-directory/fnv-diamond/analysis/pymatgen_vs_sx.py`
   (the pymatgen call and the un-rolling of the profiles - copy that logic exactly):
   - `from pymatgen.analysis.defects.corrections.freysoldt import get_freysoldt_correction`;
     ImportError -> stderr `pymatgen_defects_not_installed: ...`, exit 1;
   - `encut <= 0`: ENCUT from `INCAR` in the defect LOCPOT's folder (`pymatgen.io.vasp.Incar`),
     else 520;
   - both LOCPOTs must have the same grid shape and lattice (`np.allclose(..., atol=1e-4)`),
     else stderr `locpot_mismatch: ...`, exit 1;
   - `res = get_freysoldt_correction(q=q, dielectric=eps, defect_locpot=..., bulk_locpot=...,
     defect_frac_coords=centre, energy_cutoff=encut)`;
   - per axis a: `pd = res.metadata["plot_data"][a]["pot_plot_data"]`, un-roll `dft_diff` and
     `Vr` with `axbulk = centre[a] * lattice.abc[a]; i = argmax(x > axbulk); roll = len(x) - i;
     np.roll(v, -roll)` so index k is the point at k * L / N;
   - output one JSON line: `{"electrostatic": E_lat, "energyCutoff": encut, "modelPotentialMean":
     mean of the un-rolled Vr over all three axes, "axes": [{"length": L, "deltaV": [...],
     "modelPotential": [...]}, x3]}`.
3. `src/ScientificRuntime/Python/FnvCorrectionBridge.cpp` - like `VaspDensityGridBridge.cpp`:
   - validate first, without running Python: charge != 0, dielectric > 0 and finite, centre
     finite -> otherwise `MakePythonExecutionError(..., "python.fnv.request_invalid")`;
     a LOCPOT that does not exist -> `"python.fnv.locpot_missing"`;
   - arguments: paths as `.Utf8()`, numbers with `std::format("{:.10g}")`, centre as "fx,fy,fz";
   - `pymatgen_defects_not_installed` in the technical details ->
     `MakePythonUnavailableError(..., "python.pymatgen_defects.not_installed")` with the
     suggestion `uv pip install pymatgen-analysis-defects==2026.3.20`;
   - parse the JSON into `FnvProfiles` (charge, dielectric, centre, energyCutoff from the reply,
     positions `k * length / N`); malformed JSON or arrays of unequal length ->
     `"python.fnv.invalid_json"`.
   - `FnvCorrectionJob` in `src/ScientificRuntime/Python/FnvCorrectionJob.cpp`, a copy of
     `VaspDensityGridJob.cpp`'s pattern (throw `runtime_error(userMessage + "\n" +
     technicalDetails)` on failure, name "FNV correction", type "fnv").

## Files to create or change

Create: `src/Domain/Defects/FnvCorrection.cpp`, `scripts/python/examples/fnv_freysoldt_profiles.py`,
`src/ScientificRuntime/Python/FnvCorrectionBridge.cpp`, `src/ScientificRuntime/Python/FnvCorrectionJob.cpp`.

## Files that must NOT be touched

- the contract headers, every test file, the fixtures
- `scripts/python/fnv_sxdefectalign.py` (the oracle workflow), `test-directory/**` (read only)
- everything under `src/Presentation/`, `src/Renderer/` (the UI is task 87)
- `pyproject.toml`, `uv.lock`, `premake5.lua`, `build/`, `install/`

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=FnvCorrection*` green (golden case skipped without the env var).
2. With `DS_FNV_GOLDEN_DIR=test-directory/fnv-diamond/GeN/q-1`, the golden case passes:
   E_lat within 1 meV of 0.252685, E_corr (sxdefectalign convention, automatic plateau) within
   1 meV of 0.229874, pymatgen convention within 1 meV of 0.23324.
3. Full Release suite: no new failures.
4. `FnvCorrection.cpp` includes nothing outside Domain/Core/glm/std.

## Constraints

- Domain has no Python, IO, Renderer or Presentation includes.
- No new Python dependency; pymatgen-analysis-defects is already installed in `.venv`.
- Python: `numpy`/`pymatgen` only; no matplotlib.
