# Task 104a: phonon modes + vibronic (Huang-Rhys / PL) analysis script

## Goal
One Python script that takes a dephonopy/phonopy force-constant directory plus the ground- and
excited-state relaxations of a defect and writes a complete, self-describing result directory:
Gamma modes (frequency, irrep, localization, per-atom participation, eigenvectors), Huang-Rhys
factors S_k, S_total, Debye-Waller factor, smoothed S(E) and the harmonic PL lineshape for a
computed and an experimental ZPL. This is MVP step 1 of
`docs/work/project/plans/2026-10-06-phonons-review-and-questions.md`; the C++ bridge, job and panel
come later and only read this directory. Reference case: C2 dimer in hBN monolayer, 9x9x1,
r2SCAN, local copy in `test-directory/sjcm_mgr/PL-spectra/dimer/CH0/R2SCAN/9x9x1` (gitignored).

## Files to create or change
- `scripts/python/examples/phonon_vibronic_analysis.py` (new)
- `scripts/python/tests/test_phonon_vibronic_analysis.py` (already written - the contract, do not
  change expectations)

## Files that must NOT be touched
- anything under `src/`, `tests/`, `premake5.lua`
- anything under `Vendor/` (dephonopy, puntukas_tools2, groupy are private; if dephonopy has a bug,
  work around it in the script and report it)
- `pyproject.toml`, `uv.lock`, `install/`
- `test-directory/` (input data, read only)

## Interface

```
python scripts/python/examples/phonon_vibronic_analysis.py
    --fc-dir   <dir with dephonopy_disp.yaml / phonopy_disp.yaml + disp-NNN/vasprun.xml>
    --gs-dir   <ground-state relaxation dir: CONTCAR, OUTCAR>
    --es-dir   <excited-state relaxation dir: CONTCAR, OUTCAR>
    [--triplet-dir <single point at the es geometry, OUTCAR>]   # spin purification, see below
    [--zpl-exp <eV>]
    [--sigma <meV, Gaussian smoothing of S(E), default 6>]
    [--gamma <meV, Lorentzian ZPL broadening for PL, default 2>]
    --out-dir <dir>
```

Output directory (published atomically: write to a sibling temp dir, then replace `--out-dir`;
a failed run leaves no partial `--out-dir`):

- `summary.json`
  - `schema`: `"ds.phonon_vibronic.v1"`
  - `n_atoms`, `n_modes` (= 3 n_atoms)
  - `translations`: indices of the 3 modes treated as acoustic translations, chosen as the 3 modes
    with the smallest |frequency| (NOT positionally), and their frequencies in meV
  - `imaginary_modes`: list of `{index, freq_meV}` for non-translation modes with
    freq < -0.5 meV; `stable`: bool (true when that list is empty)
  - `s_total` (sum of S_k over non-translation modes), `debye_waller` = exp(-s_total)
  - `zpl_eV`: `{computed, computed_spin_purified, experimental}`:
    `computed = E_es - E_gs` (TOTEN, last "free  energy   TOTEN" line of each OUTCAR);
    `computed_spin_purified = 2*E_es - E_triplet - E_gs` when `--triplet-dir` is given, else null;
    `experimental` = `--zpl-exp` or null
  - `max_displacement_A` (max |dR_i| between gs and es after minimum image and centre-of-mass
    alignment), `delta_Q_amu05_A` (sqrt(sum m_i |dR_i|^2))
  - `sigma_meV`, `gamma_meV`, absolute input paths, `units`: a dict documenting every column unit
  - `approximation`: `"harmonic, equal ground/excited curvatures, displaced oscillators, T=0"`
- `modes.tsv`, one row per mode (all 3N, translations included and flagged), header line:
  `index freq_meV irrep n_eff beta S_k q_k_amu05_A is_translation`
  - `n_eff` = dephonopy `modes.iprs` (effective number of participating atoms: N for a uniform
    mode, 1 for one atom). `beta = n_atoms / n_eff`.
  - `S_k = omega_k q_k^2 / (2 hbar)`, `q_k = sum_i sqrt(m_i) e_ki . dR_i`, `e_ki` the normalised
    mass-weighted eigenvector, `dR = R_es - R_gs`. S_k = 0 for translations.
- `participation.npy`: float32 (n_modes, n_atoms), `|e_ki|^2`, each row sums to 1
- `eigenvectors.npy`: float32 (n_modes, n_atoms, 3), normalised mass-weighted `e_ki`
  (the viewer gets physical displacements as e_ki / sqrt(m_i))
- `structure.json`: `{symbols, positions_A, cell_A, masses_amu}` of the force-constant supercell
  (the geometry the modes belong to)
- `spectral_function.tsv`: `energy_meV S_E_per_meV`, Gaussian-smoothed sum of S_k delta(E - hbar w_k),
  integrates to s_total within 1 % over the written range
- `pl.tsv`: `photon_energy_eV intensity_computed intensity_spin_purified intensity_experimental`,
  T = 0 harmonic lineshape (generating function), each intensity column normalised to max 1,
  `nan` for a ZPL that was not available. The phonon sideband is on the LOW-energy side of the ZPL.

stdout: exactly one JSON line at the end, `{"ok": true, "out_dir": ..., "s_total": ..., "stable": ...}`;
on error `{"ok": false, "error": "<message>"}` and exit code 1. dephonopy's own logging goes to
stderr or is silenced - never mixed into the stdout JSON line.

## Acceptance criteria
1. `.venv/Scripts/python.exe -m pytest scripts/python/tests/test_phonon_vibronic_analysis.py -q`
   passes (the reference-data tests skip cleanly when `test-directory/` is absent).
2. Reference case (singlet relax as es, `try_1_tryplet` as triplet, zpl-exp 4.08): s_total = 1.407
   +- 0.01, the dominant mode is ~200.1 meV, irrep A1, S_k ~0.525; computed ZPL 3.452 eV,
   spin-purified 3.749 eV (both +- 0.002).
3. The script runs in under 60 s on the reference case on this PC with the app runtime
   (`install/app/python/windows/python.exe`) as well as `.venv`.
4. `.py` stays under ~500 lines.

## Constraints
- Reuse dephonopy: `ForceConstants(dir).calculate_modes()`, `modes.calculate_irreps()`,
  `modes.calculate_iprs()`, `dephonopy.hr_factors.io.read_atomic_displacements` (returns a tuple
  `(displ_bohr, init_atoms, final_atoms)`), `hr_factors_from_displacements` and
  `dephonopy.hr_factors.luminescence` for the lineshape. Do NOT `from puntukas import Atoms` - the
  local puntukas is the `puntukas_tools2` rewrite; dephonopy goes through
  `dephonopy._puntukas_compat`.
- Known dephonopy traps (from the 2026-10-06 review): `hr_factors_from_displacements` drops the
  first 3 modes positionally - compute S_k yourself from the formula above (it agrees with dephonopy
  to 1e-4 on the reference case) so the translation choice is the |freq| one; `calc_luminescence`
  defaults to ZPL = 1945 meV (NV) - always pass the ZPL; it takes a smoothed S(E), not discrete S_k;
  `modes.displacements` are physical (e/sqrt(m)) - renormalise to get e_ki.
- No MPI, no HDF5 output, no plotting, no new dependencies.
- Pure function core (`analyse(...) -> dict` plus writers) so the C++ bridge can later call the
  same script; argparse only in `main()`.
