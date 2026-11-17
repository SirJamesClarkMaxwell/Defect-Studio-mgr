# Task 113c: convergence with the supercell size n (series view + one Origin export)

Plan: `docs/work/project/plans/2026-11-17-phonon-server-results-and-comparison.md`. User 2026-11-17: a view of the
convergence over n with export. Reference: the user's `P:/PL-spectra/scripts/lineshape.py` (one Origin file per
parameter set: row 1 Long Name, row 2 Units, row 3 Comments; three column blocks of different length:
`[E-E_ZPL, E, L_n ...]`, `[hw, S_n(hw) ...]`, `[n, N_atoms, S, DW, hw_eff, E_FC, S_<irrep> ...]`). Builds on 113a
(server imports carry `supercell`), 113b (`hr_method`, `hr_cutoff_A`, `sigma_max_meV`), 113e (hidden cases).

## Goal

For the active case, the datasets that differ only in the supercell form a series. A new tab „Zbieżność” in
`Fonony: wykresy` shows the scalars against n and overlays the PL and S(ℏω) of all members with a colour gradient;
one button writes the series to one Origin file laid out like `lineshape.py`.

## Design (binding)

1. **Series** (pure function in `PhononPlotData*` - no ImGui): phonon datasets of the active case grouped by
   (hr_method, hr_cutoff_A, sigma, sigma_max, gamma, source kind); key n = supercell a (or a·b·c when a ≠ b; the
   label shows AxBxC), datasets without supercell = the FC cell (n from the structure: atom count of the FC cell is
   not enough - use the dataset's `supercell` or, if absent, label it „komórka FC” and place it first). A combo
   picks the series when there are several (default: the largest). Members sorted by n.
2. **Scalars per member**: S, DW, ℏω_eff = Σ S_k ℏω_k / S, E_FC = Σ S_k ℏω_k, S per irrep (ω > 0 modes, irreps from
   modes.tsv; include `???`), N_atoms. Computed from the loaded summary + modes (use the comparison cache of 109d /
   108e - no new cache).
3. **Plots**: (a) S, DW, ℏω_eff, E_FC vs n - four small plots or one plot with a scalar selector (choose one, say
   why in the report); (b) S_irrep vs n (stacked or lines) with a warning text when `???` carries > 10 % of S:
   „Klasyfikacja irrepów niepewna dla dużych komórek (tolerancja projekcji).”; (c) PL overlay (axis and
   normalisation as the PL tab) and (d) S(ℏω) overlay, colours from a sequential gradient (viridis-like) by n with a
   colour-bar-like legend (first, middle, last n). All through data adapters (task 110), legend popups, export
   popup per plot as the other phonon plots.
4. **Origin export** „Eksport serii (Origin)…”: one `.dat`/`.txt` TSV via `SaveDataTable` with `originHeader = true`
   (`DataTableIO` already pads ragged columns), blocks and names exactly as `lineshape.py` (`L_<AxBxC>`,
   `S(hw)_<AxBxC>`, comments `S=<value>`; first column comment = the parameter string `method cutoff σ σ_max γ ZPL`),
   E−E_ZPL grid = the PL grid of the first member mapped through each member's ZPL, interpolated (state the grid in
   the comment). Default file name `PL_<method>[_cutoff<C>A]_sig<σ>[-<σmax>]_gam<γ>.dat` in the project's
   `exports/` folder (create it), never overwriting silently (suffix `_2`, ...).

## Files to create or change

- `src/Presentation/Panels/PhononPlotData*.{hpp,cpp}` (new `PhononPlotDataSeries.cpp`), new
  `PhononModesPanelConvergence.cpp`, the plots window tab list, tests for series grouping, scalars and the export
  columns (compare against a small hand-made 3-member series)
- `docs/work/project/reports/task-113c-convergence.md`

## Files that must NOT be touched

- `scripts/`, `Vendor/`, IO readers (use them), `DataTableIO.*` (use it), renderer, energetics, structure building

## Acceptance criteria

1. Grouping: members with different method/cutoff/σ/γ land in different series; FC-cell member first.
2. Scalars match hand computation on a fixture; export header rows and column order match `lineshape.py`.
3. With the 42 server imports of the dimer (n = 9..50) the tab shows S ≈ 1.421-1.422 flat - I check by hand.
4. Existing tests pass (I build and run). Report with the click path.

## Constraints

- Layers: adapters without ImGui; main thread commits state. `.cpp` under ~500 lines.
- UI rules: labels left/above, tooltips everywhere, disabled buttons say why, paths relative to the project.
- STYLE RULE: keep existing formatting of edited files.
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild. Do not commit.
- If a binding detail conflicts with existing code, keep the user-visible behaviour, document the deviation and continue.
