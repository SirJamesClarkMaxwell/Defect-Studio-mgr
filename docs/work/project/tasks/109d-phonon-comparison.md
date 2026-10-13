# Task 109d: comparing defects - aligned PL axis, normalisation, ZPL source, summary table

Plan: `docs/work/project/plans/2026-10-13-phonon-workspace.md`. Builds on 109b (cases), 110 (`PlotData` adapters,
`DataTableIO`). User 2026-10-13: "łatwo analizować fonony [...] dla wielu defektów naraz [...] porównywanie".

## Goal

In `Fonony: wykresy` the user compares PL / S(ℏω) of several defects meaningfully: PL of different defects aligned
at their own ZPL, normalised the same way, with each curve saying which ZPL it used; and a `Porównanie` tab with one
row per phonon dataset (grouped by case) holding the numbers that go into a thesis table, exportable to TSV.

## Design (binding)

1. **Controls row of the PL tab** (persisted globally in `PhononPanelSettings`, IO keys under `view`):
   `Oś` combo `E [eV]` (today) / `E_ZPL − E [meV]` (each curve shifted by ITS OWN ZPL value of the curve it
   draws); `Normalizacja` combo `brak` / `max = 1` / `pole = 1`. Area normalisation integrates the curve on its
   FULL grid (trapezoid) before trimming to the visible range; max uses the full grid too. Implemented in the 110
   PL adapter (one place), so TSV export and figures get the same numbers. Axis label follows the mode.
2. **ZPL source per curve**: the adapter records which ZPL curve each dataset actually used
   (`PlCurveChoice` falls back silently today); when it differs from the requested choice the legend label gets
   ` (ZPL: obliczone)` / `(ZPL: po korekcji spinowej)` / ... and the series `id` stays stable. Test it.
3. **`Porównanie` tab** (Plots view, new file `PhononModesPanelComparison.cpp`):
   - rows: every phonon dataset attached to a case (grouped under the case name, active case first), then compared
     datasets attached to no case; columns: `porównaj` checkbox (global compared set), `Przypadek`, `Zestaw`,
     `ZPL [eV]` (+ source in tooltip), `ΔQ [amu^½ Å]`, `S`, `DW [%]`, `ℏω_eff [meV]`, `Najsilniejszy mod [meV]`,
     `S_k`, `n_eff`, `Mody urojone`;
   - numbers from the dataset summary (`s_total`, `debye_waller`, `zpl_eV`, `delta_Q_amu05_A`, `imaginary_modes`) and
     `modes.tsv` (strongest non-translation mode by S_k: its ℏω, S_k, effective atom count) - loaded through the
     session's existing lazy caches (`Spectra` / `PlotModes`), never per-frame file IO;
   - `ℏω_eff`: REUSE the definition already in the repo (search `scripts/python/examples/ccd_analysis.py` and
     `phonon_vibronic_analysis.py` for the effective frequency; port the same formula with a comment pointing at it;
     if the summary already stores it, read it);
   - one accent row = the dataset loaded in the active case; clicking a row activates its case and loads it;
   - `Eksport TSV…` / `Kopiuj` through 110's `DataTableIO` + the 110 export popup helper (one column per table
     column, units in the Origin header row).
4. Missing values show `-` (tooltip with the reason), never 0.

## Files to create or change

- `src/IO/PhononPanelSettingsIO.{hpp,cpp}` (two view keys; v2 stays v2 - optional keys)
- the 110 adapter files (`PhononPlotData.{hpp,cpp}`), `PhononModesPanelPlots.cpp`, new `PhononModesPanelComparison.cpp`,
  `PhononModesPanel.hpp`, session files only if a cache accessor is missing
- tests: extend `PhononPlotDataTests.cpp` (shift by own ZPL, both normalisations on a full grid, fallback label),
  new `PhononComparisonTests.cpp` (row building from synthetic datasets, strongest mode, missing values),
  `PhononPanelSettingsIOTests.cpp` (new keys roundtrip, absent -> defaults)

## Files that must NOT be touched

- `src/Domain/*`, `src/ScientificRuntime/*`, `scripts/python/*`, structure-building files
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-109d-phonon-comparison.md`

## Acceptance criteria

1. Tests above pass; existing tests unchanged.
2. Two synthetic PL curves with ZPL 1.9 and 2.5 eV plotted in `E_ZPL − E` both peak at 0 meV.
3. Report: files, tests, the ℏω_eff definition reused (file:line), anything not done.

## Constraints

- UI conventions (labels left/above, tooltips, disabled controls say why, one accent row, project-relative paths).
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
