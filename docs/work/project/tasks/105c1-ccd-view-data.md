# Task 105c1: everything the CCD view needs, from ccd_analysis.py (script side)

User 2026-10-13: "CCD dalej nie widać tych parabol, wsp. Q, jak wyglądają wzbudzenia, oraz strzałek
odpowiedzialnych za absorpcję, emisję, i możliwości wizualizacji poziomów i f.f. z nimi skorelowanych
oraz widma PL policzonego bezpośrednio z CCD". The app view is task 105c2; this task makes
`scripts/python/examples/ccd_analysis.py` publish all the data it draws, in every mode
(`--ccd-dir` path, point mode `--gs-dir/--es-dir`, `--phonon-result` effective mode).

## Goal
1. **Curves, levels and wavefunctions in every mode.** Point mode and phonon-effective mode today
   leave `ccd_curves.tsv` and `levels.tsv` empty (no Numerov). When only harmonic wells are known
   (point mode with a curvature, or the phonon effective mode), build sampled harmonic branches
   gs: `E = ½ω_g²Q²`, es: `E = ZPL_used + ½ω_e²(Q − ΔQ)²` (atomic units inside, same conversions
   as `harmonic_pl_model`; equal curvatures where only one is known) as `data` for the existing
   `numerov_model`, so the same Numerov path gives curves, levels, FC factors and the
   `intensity_numerov` PL column. For a pure harmonic input the Numerov PL must match
   `intensity_harmonic` (test). If both point-mode curvature and `--phonon-result` are given,
   the point-mode wells win (phonon_1d stays its own PL column, as today).
2. **`wavefunctions.tsv`**: `Q_amu05_A`, then `gs_0 .. gs_{k-1}`, `es_0 .. es_{k-1}` for
   k = min(8, levels in the ladder), normalised so ∫|χ|² dQ = 1 with Q in amu^0.5·Å
   (χ_Q = χ_x · sqrt(Q_AU)); grid downsampled to ≤ 2001 rows. Same Q axis as `ccd_curves.tsv`
   (gs minimum at Q = 0, es minimum at ΔQ).
3. **Computed points in point mode**: `ccd_points.tsv` rows for the single points that exist:
   `gs` at Q=0 (gs_dir), `es` at ΔQ (es_dir), `gs` at ΔQ (gs_at_es_dir), `es` at 0 (es_at_gs_dir),
   plus a `triplet` branch row at ΔQ (triplet_dir) - energies in eV relative to the gs minimum, the
   es branch shifted by the same rigid ZPL shift as the curves (status `ok`). `t` = 0 / 1.
4. **`franck_condon` block in summary.json** (eV):
   `absorption_eV` = E_es(0) − E_gs(0), `emission_eV` = E_es(ΔQ) − E_gs(ΔQ),
   `relaxation_gs_eV` = E_gs(ΔQ) − E_gs(0), `relaxation_es_eV` = E_es(0) − E_es(ΔQ),
   `zpl_eV` (= used), `delta_Q_amu05_A`, `source`: `"points"` when the vertical single points
   exist (gs_at_es / es_at_gs or CCD path points at t=0/1), else `"curves"` (read off the curves).
   All values after the rigid ZPL shift (so absorption − emission = relaxation_gs + relaxation_es).
5. Summary `units` updated; `approximation` text mentions the synthesized harmonic branches when used.

## Files to create or change
- `scripts/python/examples/ccd_analysis.py`, `scripts/python/tests/test_ccd_analysis.py`

## Files that must NOT be touched
- everything outside those two files (another Codex session is editing `src/` and `tests/` now);
  `Vendor/` is read-only (puntukas API: `solve_bound_states` returns `BoundStates` with
  `x`, `potential`, `energies`, `wavefunctions` rows normalised over x).

## Acceptance criteria
1. `.venv/Scripts/python.exe -m pytest scripts/python/tests -q`: new tests pass, nothing that passed
   fails (known: `test_check_include_directions::test_disallowed_edge_detected`).
2. Synthetic harmonic test: Numerov PL vs harmonic PL max abs difference < 1e-2 (normalised); levels
   ≈ (n+½)ħω within 0.1 %; wavefunctions normalised (trapezoid ∫|χ|²dQ = 1 ± 1e-3).
3. V2CBCN dimer point-mode reference (existing test data, `--zpl-formula "2*E_es - E_T - E_gs"`):
   `franck_condon` satisfies absorption − emission = relaxation_gs + relaxation_es (1e-9),
   ΔQ 0.3956, curves/levels/wavefunctions non-empty, `ccd_points.tsv` has 5 rows.
4. Report the dimer's absorption, emission, both relaxations and ħω_gs/ħω_es.

## Constraints
- stdlib + numpy/scipy/puntukas/ase as already imported. No `eval`.
- PowerShell built-ins only; do not edit UTF-8 files with Get-Content/Set-Content.
