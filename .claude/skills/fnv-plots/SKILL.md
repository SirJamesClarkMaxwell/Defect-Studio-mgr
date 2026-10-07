---
name: fnv-plots
description: "Use when the user wants plots of an FNV / sxdefectalign analysis (potential profiles vline-eV-a*.dat, atomic-site potentials vAtoms.dat, plateau window, comparison of alignment constants or corrections between defects/charge states). Python + matplotlib, PNG output for reports."
---

# fnv-plots

Per-run figures come from `scripts/python/fnv_sxdefectalign.py plot <dir> --out <dir> --title T`
(any folder with `vline-eV-a{0,1,2}.dat`, optionally `vAtoms.dat`; LOCPOT not needed).
`run` makes `pass1.png` / `pass2.png` the same way. Read `fnv-correction` first for conventions.

Figure layout (keep it when adding plots): one panel per lattice axis - `V_model` (blue
`#2a78d6`), `ΔV DFT` (orange `#eb6834`), `ΔV - V_model` (aqua `#1baf7a`); plateau window grey
shading; dashed line = C of that axis; dotted line = defect position; title carries C, σ and
"brak plateau!" when the window is not flat. Fourth panel: vAtoms `ΔV - V_lr` vs distance,
far shell dark. Units in labels (bohr, eV). Polish labels - the user reads them.

Summary/comparison plots across cases: read `summary.json` / `plateau.json`, one chart per
measure (never two y-axes), categorical colors in the fixed order above, a legend whenever
>= 2 series. Save PNG (dpi 130) next to the data in `test-directory/fnv-diamond/analysis/`,
then Read the PNG and look at it before reporting (label collisions, empty panels).
