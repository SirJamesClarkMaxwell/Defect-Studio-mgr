# Task 106a: bulk phonon dispersion script

## Goal
`scripts/python/examples/phonon_dispersion.py` computes the phonon band structure omega(q) along a
high-symmetry path from a dephonopy finite-displacement directory (bulk supercell FC, e.g.
`test-directory/sjcm_mgr/hBN/bulk/phonons/vasp_661`) and writes plain tables the C++ panel will
plot (task 106b). It reproduces the user's own script
`test-directory/sjcm_mgr/hBN/bulk/phonons/vasp_661/phonon-dispersion.py`
(`plot_bulk_bandstructure(fc, unit_cell=fc.atoms)`, picture `dispersion_test.png` next to it).

## Files to create or change
- `scripts/python/examples/phonon_dispersion.py` (new)
- `scripts/python/tests/test_phonon_dispersion.py` (new; see acceptance criteria)

## Files that must NOT be touched
- `Vendor/`, `src/`, `tests/`, `premake5.lua`, `install/`, `test-directory/`, other scripts

## Interface
```
phonon_dispersion.py --fc-dir DIR [--npoints 101] [--no-symmetrize] --out-dir DIR
```
Same house style as `phonon_vibronic_analysis.py`: argparse errors as JSON, library chatter to
stderr, atomic publish (reuse `phonon_vibronic_analysis.publish(result, out_dir, writer=...)`),
last stdout line `{"ok": true, "out_dir", "n_bands", "min_freq_meV", "max_freq_meV"}` or
`{"ok": false, "error"}` (exit 1).

## Physics / library use
- `dephonopy.force_constants.ForceConstants(fc_dir)`, `ensure_symmetry()` unless
  `--no-symmetrize`, then `fc.Phonopy(unit_cell=fc.atoms)` exactly like
  `dephonopy.bulk_phonon_plotter.bulk_phonons.plot_bulk_bandstructure` does (read it), then
  `auto_band_structure(npoints=...)` (seekpath path on the primitive cell) and
  `get_band_structure_dict()`. Do not use matplotlib.
- Energies in meV (`dephonopy._puntukas_compat.Units.convert_vasp_energy(..., "meV")`, as the
  plotter does). Imaginary modes stay negative.
- Segment joining and high-symmetry labels: same logic as the plotter (labels joined with "/" at
  path breaks, "GAMMA" -> "Γ").

## Output (`--out-dir`)
- `dispersion.tsv`: header `segment\tdistance\tband_0\t...\tband_{n-1}`, one row per q-point,
  `segment` = index of the continuous piece (a new piece after each path break), `distance` the
  cumulative path coordinate from phonopy.
- `summary.json`, schema `ds.phonon_dispersion.v1`: `n_bands`, `n_atoms_primitive`,
  `ticks` [{"distance", "label"}], `breaks` [distance...], `min_freq_meV`, `max_freq_meV`,
  `gamma_freq_meV` (frequencies at the first Γ on the path), `primitive_cell_A`,
  `primitive_symbols`, `input_paths`, `units`, `symmetrized`.

## Acceptance criteria
1. Tests in `scripts/python/tests/test_phonon_dispersion.py` (skip when the data is absent):
   - the hBN bulk run is ok; `n_bands == 3 * n_atoms_primitive`;
   - first tick label is "Γ"; three acoustic branches at that Γ within 1 meV of zero;
   - `max_freq_meV` between 150 and 220 (hBN optical top ~ 200 meV);
   - `dispersion.tsv` has `n_bands + 2` columns, distances non-decreasing;
   - a missing directory gives `{"ok": false}` and exit 1, nothing published.
2. `.venv/Scripts/python.exe -m pytest scripts/python/tests -q`: no new failures
   (`test_check_include_directions::test_disallowed_edge_detected` fails on the base already).
3. Report the band-top and the tick labels you get, so the reviewer can compare with
   `dispersion_test.png`.

## Constraints
- No new dependencies (phonopy, seekpath, dephonopy are installed in `.venv`).
- In this sandbox Git Bash tools crash; use PowerShell built-ins.
