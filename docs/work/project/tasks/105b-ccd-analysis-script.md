# Task 105b: 1D CCD analysis - harmonic fits, Numerov, PL

## Goal
`scripts/python/examples/ccd_analysis.py` turns the single points produced from
`ccd_inputs.py` (task 104e) layouts into a 1D configuration-coordinate model: energies per point
with a convergence status, harmonic fits around both minima (hbar omega, S, relaxation energy),
Numerov vibrational levels and Franck-Condon factors on the interpolated curves
(`puntukas.vibronic`), and 1D PL line shapes. Without CCD points it can still build the
equal-curvature 1D effective mode from a phonon result (task 104a output). Reference systems: the
hBN C2 dimer and the divacancy+dimer (V2CBCN) - their CCD points do not exist yet, so the tests use
synthetic wells; the VV data in `test-directory/` is only a reader check (it has a NELM-hit point
and a killed one).

## Files to create or change
- `scripts/python/examples/ccd_analysis.py` (new)
- `scripts/python/examples/phonon_vibronic_analysis.py` - ONLY if you reuse its `publish()`:
  give it a `writer` parameter defaulting to `write_result`; behaviour unchanged otherwise

## Files that must NOT be touched
- `scripts/python/tests/*` (the contract: `test_ccd_analysis.py`)
- `Vendor/`, `src/`, `tests/`, `premake5.lua`, `install/`, `test-directory/`

## Interface
```
ccd_analysis.py [--ccd-dir DIR] [--phonon-result DIR] [--zpl EV] [--sigma MEV=6]
                [--temperature K=0] [--n-states N] --out-dir DIR
```
At least one of `--ccd-dir` / `--phonon-result`. stdout ends with one JSON line
`{"ok": true, "out_dir", "zpl_eV": <used>}` or `{"ok": false, "error"}` (exit 1, nothing
published). Publish atomically like `phonon_vibronic_analysis.publish` (stage a sibling, swap).

## Reading `--ccd-dir`
- `ccd_path.csv` (`branch,t,Q_amu05_A`), point directory `<branch>/t_{round(100 t):+04d}`.
- Energy = the LAST line matching `free  energy   TOTEN` (two spaces, three spaces) in OUTCAR.
  The per-SCF-step lines `free energy    TOTEN` (four spaces) are NOT results - a killed run has
  only those. No final line -> status `missing`, `energy_eV` null.
- `scf_steps` = number of `DAV:`/`RMM:` lines in OSZICAR after the last-but-one `F=` (for NSW=0:
  all of them); `nelm` from INCAR (`NELM = n`, VASP default 60). `scf_steps >= nelm` ->
  `unconverged`. Otherwise `ok`. Only `ok` points enter fits and splines.
- Fewer than 3 `ok` points in a branch -> error naming the branch (`gs`/`es`).
- Energies relative to the gs minimum (lowest ok gs energy). `zpl_eV.from_ccd` =
  min(es) - min(gs). `delta_Q` = Q of the es minimum minus Q of the gs minimum.

## Model
- Harmonic fit per branch: quadratic least squares through the branch minimum and its two
  nearest ok neighbours (one each side when available). hbar omega from k = d2E/dQ2 (Q in
  amu^1/2 A, E in eV); `S = dQ^2 omega / (2 hbar)` with that branch's omega;
  `relaxation_eV = S * hbar omega`.
- Rigid shift (decision of 2026-10-12, option A): the es curve is shifted so its minimum is at
  `zpl_used` = `--zpl` if given, else `from_ccd`. Without CCD points: `--zpl`, else the phonon
  result's `computed_spin_purified`, else its `computed`.
- Numerov: both curves on ONE shared grid (puntukas requires it), cubic spline through ok points,
  continued outside the data by the quadratic (value, slope, max(curvature, small>0)) at each edge
  so the wells stay bound; grid wide enough for the requested levels (check the highest level's
  turning points lie inside). Convert Q to atomic units (`puntukas.units.convert`, mass-weighted,
  m = 1). `solve_bound_states` (gs: enough states that `fc_sum` > 0.99, es: >= 3 plus thermal
  need), `franck_condon_model(es, gs)` (emission: initial = es), `luminescence_band(...,
  temperature=K)` with an explicit positive photon-energy grid.
- `harmonic` PL: `puntukas.vibronic.harmonic_model(omega_es, omega_gs, dQ)` from the fits.
- `phonon_1d` (`--phonon-result`): from `modes.tsv`, non-translation rows,
  `dQ^2 = sum q_k^2`, `omega_eff^2 = sum omega_k^2 q_k^2 / dQ^2`, `S = dQ^2 omega_eff/(2 hbar)`;
  PL from `harmonic_model(omega_eff, omega_eff, dQ)`.

## Output (`--out-dir`)
- `summary.json`, schema `ds.ccd_analysis.v1`: `points` [{branch, t, Q_amu05_A, energy_eV|null,
  status, scf_steps, nelm}], `delta_Q_amu05_A`, `zpl_eV` {from_ccd|null, used},
  `harmonic` {gs, es: {hbar_omega_meV, S, relaxation_eV}} | null, `numerov` {gs_levels_meV,
  es_levels_meV (from each well's own bottom), fc_0n (es v=0 -> gs n), fc_sum} | null,
  `phonon_1d` {hbar_omega_eff_meV, delta_Q_amu05_A, S} | null, `sigma_meV`, `temperature_K`,
  `input_paths`, `units`, `approximation`.
- `ccd_points.tsv` (branch, t, Q_amu05_A, energy_eV, status), `ccd_curves.tsv` (Q_amu05_A,
  V_gs_eV, V_es_eV on the Numerov grid, es shifted), `levels.tsv` (branch, n, energy_meV),
  `pl_1d.tsv` with header exactly `photon_energy_eV intensity_numerov intensity_harmonic
  intensity_phonon_1d` (tabs), ascending positive energies, each present column max 1, absent
  columns NaN.

## Acceptance criteria
1. `.venv/Scripts/python.exe -m pytest scripts/python/tests/test_ccd_analysis.py -q` green.
2. `.venv/Scripts/python.exe -m pytest scripts/python/tests -q`: no new failures
   (`test_check_include_directions::test_disallowed_edge_detected` fails already on the base).
3. A runnable check: `ccd_analysis.py --self-test` not required; the test file is the check.

## Constraints
- Use `puntukas.vibronic` and `puntukas.units` (installed editable in `.venv`); do not
  re-implement Numerov, overlaps or line shapes. scipy for splines/fits.
- No new dependencies. ASCII/LF output. Keep the script well under ~500 lines.
- In the sandbox Git Bash tools crash; use PowerShell built-ins.
