---
name: fnv-correction
description: "Use when computing, checking or explaining an FNV (Freysoldt) electrostatic charge correction for a charged defect supercell in this repo: running the original sxdefectalign binary (Linux, via WSL Ubuntu) on LOCPOT files, choosing the plateau/alignment constant -C, reproducing the BSc-thesis diamond numbers (GeN, GeV, SiV, GeNV, ...), or validating the future DefectStudio FNV engine against sxdefectalign. Also use for the plots of vline-eV-a*.dat / vAtoms.dat (see fnv-plots)."
---

# fnv-correction

Runs the thesis workflow (two sxdefectalign passes) with the plateau proposed automatically.
Tool: `scripts/python/fnv_sxdefectalign.py` (`run` = both passes + figures, `plot` = figures only).
Python: `.venv/Scripts/python.exe` (numpy + matplotlib). Design background:
`docs/work/project/plans/2026-10-05-fnv-charge-correction.md`, `2026-10-06-fnv-review.md`.

## Physics conventions (do not get these wrong)

- **Charge sign is inverted**: sxdefectalign `--charge` = number of *excess electrons* = `-q`.
  q = -1 -> `--charge 1`; q = +1 -> `--charge -1`; q = -2 -> `--charge 2`. The script takes VASP
  `q` in `case.json` and flips it. Never flip it again by hand.
- `--ecut` is in **Ry** (44.11 Ry = 600 eV, ENCUT of the thesis). `--eps` scalar, diamond 5.7
  (value from the supervisor - record `eps_source`).
- `--center` fractional (`--relative`), comma-separated, no spaces. Substitutional (GeN): between
  Ge and N; split vacancy (GeV, SiV): the bond centre between the two empty sites = Ge/Si position.
  The centre does not change E_lat, only the profiles and the alignment.
- LOCPOT must be written with `LVHAR=.TRUE.` (Hartree + ionic). With `ISPIN=2` sxdefectalign reads
  the **first** block; the reference (bulk) LOCPOT must be the same supercell and grid.
- vline-eV-aN.dat: block 1 `z, V_model`; `&`; block 2 `z, ΔV_DFT, ΔV_DFT - V_model` (bohr, eV).
  vAtoms.dat: `r, V_lr, ΔV, ΔV - V_lr, x, y, z`.
- Pass 1 (no `-C`): `Defect correction` = screened lattice energy E_lat (alignment 0).
  Pass 2 (`-C <plateau of ΔV - V_model>`): final E_corr = E_lat - charge_sx·C = E_lat + q·C
  (checked: GeN q=-1, 0.252685 - 0.022811 = 0.229874).
  E_corr is *added* to E_tot(defect) in the formation energy; do not add alignment twice.

## Plateau proposal (replaces reading it off Origin)

Per axis: defect position = peak of |V_model|; window centred on the farthest point (periodic),
widened while std(ΔV - V_model) <= `--tol` (default 0.01 eV), min width `--min-width` (4 bohr).
C = mean of the three axis window means. Diagnostics: per-axis σ, spread between axes, the
vAtoms far-shell mean (r >= 0.75 r_max). "brak plateau!" on an axis = the residual is not flat:
supercell too small or charge delocalised - report it, never hide it.
**Localisation check (always report it):** slope of ΔV_DFT vs V_model outside the 5 bohr core
(`slope` in the summary, shown as `ΔV/V_model` in plot titles). ~1 = localised charge, FNV valid;
0.5-0.9 = partly delocalised, the point-charge model over-corrects; ~0 = delocalised (shallow /
band-like state), FNV meaningless - the charge state is suspect, not just its correction.
Diamond 2026-10-06: Ge_C/Si_C q=±1 ~0, GeN2V/SiN2V q=-1 ~0.2-0.3, vacancy complexes q=+1 0.55-0.83. Manual override: set
`"C": <value>` in `case.json`, the result records `C_source: manual`.

## Run

```
.venv/Scripts/python.exe scripts/python/fnv_sxdefectalign.py run <case.json> --out <dir>
```
`case.json` (paths relative to it): `name, q, eps, eps_source, ecut_ry, center[3], vdef, vref,
sxdefectalign, C(null|value)`. Output: `pass1/`, `pass2/` (sxdefectalign files + log),
`pass1.png`, `pass2.png`, `summary.json` (E_lat, C_used, E_corr, plateau stats). One run on
180³ LOCPOTs takes ~1-2 min per pass in WSL; never run several big cases in parallel (RAM).

## Test data (local only, gitignored)

`test-directory/fnv-diamond/` - copy of the thesis data (source
`D:\STUDIA\Fizyka\01_Engeenering-Studies\Praca-Inżynierska\Diament`, **read-only, never write
there**): `bin/sxdefectalign`, `GeN/q-1/{LOCPOT-bulk,LOCPOT_1-,case.json,reference/}`,
`GeV/q+1|q-2/reference/` (vline only, no LOCPOT), `structures/`, `analysis/` (outputs).
New defects from Okeanos go in as `<defect>/q<sign><n>/` with a `case.json` each.

Golden GeN q=-1 (pass 1, reproduced bit-exactly vs 2025 files): `--charge 1 --eps 5.7
--center 0.461240,0.559967,0.461206` -> `Defect correction 0.252685 eV`, isolated 0.398942 Ha,
periodic 0.346012 Ha, unscreened difference -1.4403 eV.

## Checklist when reporting a result

q (VASP) and the passed `--charge`; ε + source; centre + source; per-axis C, σ, flat flags;
E_lat, C_used (auto/manual), E_corr; the figures. Results are a model, not a verdict - show
both charge variants / manual vs auto C when they disagree.

## Data on Okeanos (O: drive)

`Mount-Okeanos` (PowerShell profile) maps `O:` = `~/Diament` on Okeanos (all thesis defects,
`<defect>/Stan_<q>/HSE_Normal/LOCPOT`, GeV in `Stan_*/HSE/PrecFock_Normal`). SSHFS runs at
~10 KB/s: **never copy LOCPOTs**. Run sxdefectalign on Okeanos instead
(`~/Diament/_fnv-analysis-2026-10-06/run_fnv.sh` + `cases.tsv`, pass 1 only; the user starts it
over SSH), copy back `runs/*/` (vline/vAtoms/log, ~70 KB each) and analyse locally with
`test-directory/fnv-diamond/analysis/okeanos_summary.py`. Pass 2 is redundant: E_corr is linear
in C. With `-C`, sxdefectalign shifts the V_model block by +C (that is how a past -C is recovered
from first/second outputs). Verify q from `NELECT` in OUTCAR vs the neutral `Stan_0`.
