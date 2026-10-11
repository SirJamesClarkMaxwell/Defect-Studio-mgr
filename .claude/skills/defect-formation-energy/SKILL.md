---
name: defect-formation-energy
description: "Use when computing, checking or reproducing defect formation energies E_f(E_F), charge-transition levels or binding energies from raw VASP outputs in this repo - in particular reproducing the BSc-thesis diamond results (GeV, SiV, GeN, GeNV, GeN2V, Ge_C, N_C and Si analogues) first with sxdefectalign E_corr and then with the DefectStudio FNV panel E_corr. Covers chemical potentials (O:\\Potencjaly_chemiczne), VBM, sign conventions, the reference curves and the acceptance tolerances."
---

# defect-formation-energy

Read `fnv-correction` first (E_corr, charge sign, plateau). This skill adds the energies on top.

## Formula (thesis eq. "Formation Energy", the contract)

E_f[X^q](E_F) = E_tot[X^q] - E_tot[bulk] - sum_i n_i mu_i + q (E_VBM + E_F) + E_corr[X^q]

- E_tot = last `free  energy   TOTEN` in OUTCAR (not `energy  without entropy`, not sigma->0).
- n_i = atoms added (+) / removed (-) vs the 512-atom bulk. Count species from CONTCAR line 6-7
  vs bulk - never hand-type them. GeV: C -2, Ge +1. GeN: C -2, Ge +1, N +1.
- mu_C = E_tot[bulk] / 512 (same calculation as the reference). mu_Ge, mu_Si, mu_N from
  `O:\Potencjaly_chemiczne\{Ge,Si,N}` - several convergence variants exist; the final one is
  chosen by the user, record the folder (`mu_source`). Never fit mu to the thesis curves.
- E_VBM = HOMO at Gamma of the bulk 512 run (EIGENVAL/OUTCAR, highest occupied band, both
  spins if ISPIN=2). E_F runs 0 .. E_g, thesis E_g = 5.34 eV (HSE); reference curves go to 5.47.
- q from NELECT vs the neutral run of the same defect (q = NELECT_0 - NELECT_q). Folder names
  `Stan_1-` = q -1. Never trust a label over NELECT.
- E_corr = E_lat + q*C (fnv-correction). E_corr = 0 for q = 0. Neutral and charged runs of one
  defect must share ENCUT, functional (HSE, HFSCREEN), PREC, k-mesh - check INCAR before mixing.
- Transition level eps(q1/q2) = (E_f(q1; E_F=0) - E_f(q2; E_F=0)) / (q2 - q1). Independent of mu.
- Binding: E_b = E_f(A) + E_f(B) - E_f(AB) at the same E_F (thesis eq. energia_wiazania).

## Thesis reference values

- `D:\STUDIA\Fizyka\01_Engeenering-Studies\Praca-Inżynierska\Engeenering-thesis\defects_creation_energy.json`:
  lower-envelope curves on E_F = 0..5.47 step 0.01 for GeV, SiV, GeNV, SiNV (GeV(0) = 7.57427,
  GeV(5.47) = 1.95744). Matches `img/Defects/Formations_Energy/G4V - formation.png`.
- Other defects: only PNG figures in `Theisis/theisis/img/Defects/{Formations_Energy,Transition_Energy,Binding_Energy}`.
- `formation_energies.csv` and `out_tables/*_deep.csv` in the thesis folder are toy donor/acceptor
  demos, NOT results.
- Thesis manual C for GeV: q+1 -0.05, q-1 +0.0025, q-2 +0.02. Other C were never saved.
- SiV is not on O: (no Stan_* data) - it can only be compared, not recomputed.

## Data

- Okeanos mount `O:` = `~/Diament` (read-only, guarded by `.claude/hooks/readonly_data_guard.py`).
  OUTCAR/EIGENVAL/CONTCAR are small enough to read over SSHFS; LOCPOT/CHGCAR are not.
- Local copies of every Stan_* run + bulk: `test-directory/fnv-diamond/okeanos/data/<defect>/q<±n>/`
  (`SOURCE` file names the O: folder; bulk md5 66e986... identical in all copies).
- sxdefectalign E_lat / auto C for 22 cases: `test-directory/fnv-diamond/analysis/okeanos/results.csv`.

## Procedure (A = oracle, B = app)

1. Extract raw numbers: `formation_energy_repro.py` reads OUTCARs itself; `outcar-extractor`
   (haiku) gives a second, independent table - diff the two by script (2026-10-10 the agent
   invented 23/31 TOTENs when it retyped numbers; never trust agent-typed numbers). Per run TOTEN, NELECT, species counts,
   ENCUT/LHFCALC/HFSCREEN/ISPIN, convergence flag; bulk HOMO at Gamma; mu candidates.
2. User confirms mu folders. Record mu + source.
3. A: E_corr from sxdefectalign (`fnv-oracle-runner`, haiku; or results.csv + chosen C).
   B: E_corr from the DefectStudio FNV panel (same LOCPOTs, same centre, same eps 5.7, ecut).
4. Compute E_f lines, envelope, transition levels for A and B into
   `test-directory/fnv-diamond/analysis/formation/`.
5. Independent check (`fnv-crosscheck-reviewer`, sonnet): recomputes from raw numbers without
   reusing step-4 code, compares A vs B and A vs thesis.

## Commands

```
# A: E_f from sxdefectalign E_corr (CSV columns tag,E_corr_eV), thesis-style plots + side-by-side PDF
.venv/Scripts/python.exe scripts/python/formation_energy_repro.py --ecorr <csv> --label "<label>" --out <dir>
# B: the FNV panel's default calculation, headless, through the real C++/pymatgen code path
#    (cases TSV: tag, q, fx,fy,fz, defect LOCPOT, bulk LOCPOT; ~1 min per case, Release build)
DS_FNV_BATCH_CASES=<tsv> DS_FNV_BATCH_OUT=<dir> build/bin/Release-windows-x86_64/DefectStudioTests/DefectStudioTests.exe \
    --gtest_filter=FnvCorrectionBridgeTests.DiamondBatchForFormationEnergies
.venv/Scripts/python.exe scripts/python/fnv_app_vs_sx.py <dir> <A results.csv> <A runs dir>   # B vs A
```
Outputs live in `test-directory/fnv-diamond/analysis/formation/{A_sx_autoC,A_sx_thesisC,B_app}/`.
The user clicks the panel only at the very end, once B vs A has passed.

## Acceptance (user decision 2026-10-10)

- B vs A, same C: |dE_corr| <= 1 meV per case.
- A vs thesis: |dE_f| <= 10 meV along the curve (thesis C was hand-picked from Origin).
- Anything over: report the case, the term that differs (E_tot, mu, VBM, E_corr), never tune a
  parameter to hide it. Delocalised cases (fnv-correction slope ~0) are flagged, not "fixed".

## Report checklist

Per defect/q: run folder, NELECT, q, E_tot, n_i, E_corr (A, B, C used + source), E_f(E_F=0).
Global: mu_i + source folder, E_VBM, E_g, eps + source. Plots: A, B, thesis on one axes per
defect family (see `fnv-plots` styling).
