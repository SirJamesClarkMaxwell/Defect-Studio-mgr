# Task 104f: force-constant embedding in the phonon/vibronic script

## Goal
`scripts/python/examples/phonon_vibronic_analysis.py` (task 104a) can optionally embed the defect
force constants into a larger bulk supercell (dephonopy, serial, no MPI) and do the whole mode +
Huang-Rhys + PL analysis in that larger cell. The origin offset between the defect cell and the
bulk cell is found automatically - that offset is why the user's `hBN/scipts/embeding.py` failed on
the cluster (every site was flagged as a defect, dcutoff became negative).

## Files to create or change
- `scripts/python/examples/phonon_vibronic_analysis.py`
- `scripts/python/tests/test_phonon_vibronic_analysis.py` - ADD the new test class below; do not
  change existing tests

## Files that must NOT be touched
- everything under `src/`, `tests/`, `Vendor/`, `install/`, `test-directory/`, `pyproject.toml`

## New command-line options
```
[--bulk-fc-dir <dir>]        # bulk force constants (dephonopy/phonopy dir), enables embedding
[--mult NxMxK]               # embedded supercell in units of the bulk primitive cell, e.g. 24x24x1
[--dcutoff <A>] [--bcutoff <A>]   # optional, dephonopy defaults otherwise
```
`--bulk-fc-dir` and `--mult` go together (error if only one is given).

## Behaviour
1. Read defect FC (`--fc-dir`) and bulk FC as now; `ensure_symmetry()` both.
2. Primitive cell of the bulk: from the bulk FC (phonopy primitive / spglib on the bulk supercell -
   find what dephonopy/phonopy already exposes; for hBN it is the 2-atom cell a = 2.5 A, c = 15 A).
3. **Origin alignment**: find the translation t (Cartesian, modulo the primitive lattice) that maps
   the defect-cell sites onto the bulk lattice: for each candidate t = r_def(i0) - r_bulk(j) over
   bulk sites j of the species of a reference defect atom i0, score = number of defect atoms whose
   species matches the nearest bulk site within 0.3 A (minimum image) after shifting by -t; take the
   best. Shift the defect FC atoms AND the ground-state geometry by -t and wrap. Report t in the
   summary. Reference case: t = (1.25, 0.7217, 0) A modulo the hBN lattice (B of the defect cell sits
   on the N position of the bulk cell).
4. `dephonopy.embedding.defect_fc.construct_and_embed_defect_serial(bulk_fc, mult, defect_fc,
   unit_cell=primitive, dcutoff=..., bcutoff=...)`, then `big_fc.ensure_symmetry()`,
   `calculate_modes(defect_info=info)`, irreps, IPRs.
5. Displacements: `read_atomic_displacements(gs, es)` as now, then embed into the big cell with
   `dephonopy.hr_factors.fc_displacements.compare_systems_and_embedd_array(modes, displ,
   small_atoms=<shifted gs>, cutoff=None, defect_sites=None, bulk_atoms=None, nm="displacements")`
   (defect sites come from `modes.defect_info`). S_k from the same formula as now on the embedded
   dR; keep the cross-check against `hr_factors_from_displacements(modes, displ, small_atoms=gs)`.
6. Everything written to the result directory refers to the embedded cell (structure.json,
   modes.tsv, eigenvectors, participation). `summary.json` gets
   `"embedding": {"bulk_fc_dir", "mult": [n, m, k], "origin_shift_A": [x, y, z], "dcutoff_A",
   "bcutoff_A", "defect_sites": n}` - `null` without embedding.
7. **New for both modes (embedded or not):** write `POSCAR` (VASP 5, Cartesian or Direct, atoms in
   exactly the structure.json order - repeat species names in the species line when the order is not
   grouped, e.g. `B C N B`) so the app can open the cell the modes belong to.
8. Imaginary modes: in large 2D cells the long-wavelength flexural (ZA) modes come out at a few meV
   imaginary (36x36x1: -4.2 meV). Keep reporting them in `imaginary_modes`; do not drop them; exclude
   them from S_total and the spectra only if their S_k is not finite.

## Acceptance criteria
1. Existing tests still pass; the new class passes:
```python
@unittest.skipIf(IMPORT_ERROR is not None, ...)
@unittest.skipUnless(<reference data present> and <bulk FC dir test-directory/sjcm_mgr/hBN/bulk/phonons/vasp_661 present>, ...)
class EmbeddingTests(unittest.TestCase):
    # run with --bulk-fc-dir .../hBN/bulk/phonons/vasp_661 --mult 12x12x1 (+ the usual gs/es/triplet/zpl-exp)
    # n_atoms == 288; summary["embedding"]["defect_sites"] == 2
    # origin_shift_A within 0.05 A of (1.25, 0.7217, 0) modulo the primitive lattice (minimum image in the
    # primitive cell; t comes from a relaxed atom, so ~0.02 A relaxation noise and the ~0.009 A slab z shift are expected)
    # s_total == 1.4217 +- 0.005 ; dominant mode 199.81 +- 0.1 meV, A1, S_k 0.537 +- 0.005
    # POSCAR exists, 288 atoms, species sequence matches structure.json
    # without embedding the summary has "embedding": null and a POSCAR with 162 atoms
```
   (Reference numbers computed independently with dephonopy's own `hr_factors_from_displacements`.)
2. 12x12x1 runs in under 30 s with `.venv`.
3. Script stays under ~500 lines.

## Constraints
- Serial dephonopy path only; no MPI, no scalapack.
- Do not mutate the defect FC used for the non-embedded path in a way that changes the 104a results.
