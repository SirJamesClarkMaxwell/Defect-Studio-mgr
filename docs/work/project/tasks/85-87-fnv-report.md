# Tasks 85-87 report - FNV steps 3 and 4 (2026-10-08 night)

Branch chain: `task/84-hide-volumes` -> `task/85-density-centre` -> `task/86-fnv-engine` ->
`task/87-fnv-panel` (each off the previous; `task/87-fnv-panel` holds all of them). Contracts by
Claude, implementation by Codex, build/review/validation by Claude. Nothing pushed or merged.

## How to try it (GeV)

1. Open the GeV q-1 structure (`test-directory/fnv-diamond/okeanos/data/GeV/q-1/CONTCAR`).
2. Add > Gęstość z CHGCAR... -> `.../GeV/q-1/CHGCAR`, component magnetization. In the N panel,
   section "Lokalizacja (FNV)": the centre is proposed on the Ge site, "W kuli R" ~93 % at
   R = 5 Å, the cumulative curve with R draggable; the R sphere shows while the density is selected.
3. Widok > Analiza > FNV Correction. "..." next to "LOCPOT defektu" -> `.../GeV/q-1/LOCPOT`: q becomes -1,
   the bulk LOCPOT `.../data/bulk/LOCPOT` is found by itself. "Z gęstości" copies the centre from
   the selected density. "Oblicz profile" (~10 s). Then drag a window edge on a profile tab: C and
   E_corr follow at once.

## Validation

| Case | E_corr app (sxdefectalign convention, auto plateau) | sxdefectalign |
|---|---|---|
| GeN q-1 (thesis LOCPOTs) | 0.22958 eV | 0.22987 eV |
| GeV q-1 | 0.24983 | 0.25001 |
| GeV q-2 | 0.97869 | 0.97935 |
| GeV q+1 | 0.21001 | 0.21016 |

E_lat differs by -0.15 meV (|q| = 1) / -0.6 meV (|q| = 2) - pymatgen's own Ewald/model sum;
everything else is the same within 1e-6 (plateau maths checked on sxdefectalign's own vline
output). Localisation (|m| within 5 Å of its centroid): GeV q-1 0.93, GeV q+1 0.90, Ge_C q+1 0.21,
Ge_C q-1 0.25 - the delocalisation warning separates them at 0.5.

## Decisions taken without you - check them

- **G = 0 convention defaults to sxdefectalign** (switchable in the panel to pymatgen). It is the
  one that reproduces the thesis; pymatgen's divides the model's constant term by ε and gives
  E_corr +3.4 meV (|q| = 1) / about +14 meV (|q| = 2) higher. This was the open question in
  `2026-10-06-fnv-review.md`; physically the pymatgen one is the consistently screened model, so
  this deserves a word with the supervisor.
- ε defaults to 5.7 with source "promotor (diament)".
- q is read from the folder name (q-1, Stan_1-, stan_+1); not from OUTCAR NELECT yet.

## Environment

`install/app/python` (gitignored) was refreshed with `scripts/python/prepare_app_python_runtime.py`
so the app's Python has pymatgen-analysis-defects 2026.3.20. Another machine needs the same.

## Not done

- FNV inputs/results are not saved with the project (session only).
- No viewport marker for the FNV centre other than the density's R sphere.
- Formation/binding energies and Origin/CSV export (plan `2026-10-06-defect-energies-and-origin-export.md`).
- The OpenGL counter-readback warning spam on the iso slider (`2026-10-07-density-followups.md`).
