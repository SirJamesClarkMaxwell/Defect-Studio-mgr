# Task 108e: compare the GS -> ES displacement (ΔR) between defects

Builds on 108b (`CcdDisplacement` loaded with a CCD dataset: symbols, masses, cell, GS positions, ΔR, ΔQ) and the
109d comparison (`Porównanie` table, `PhononComparisonRow`, compared datasets). User decision 2026-10-20: the
comparison adds ΔR localisation, how ΔQ² splits over modes, and the ΔR overlap between cases with the same structure.

## Goal

In `Fonony: wykresy` → Porównanie the user sees, per compared case, how localised the relaxation is and how
complete the mode decomposition is, plus a localisation plot ΔQ²(r) with one curve per case and a small overlap
matrix for cases whose structures are comparable. Everything exports through the existing data adapters (task 110).

## Design (binding)

1. **Domain** (new `src/Domain/Vibrations/DisplacementAnalysis.{hpp,cpp}`, pure, no IO):
   - `DisplacementCentre(const CcdDisplacement &)`: the ΔQ²-weighted centre of the GS positions
     (w_i = m_i|ΔR_i|²), computed with minimum-image offsets relative to the atom with the largest w_i so a defect
     at a cell boundary does not average across the cell.
   - `LocalisationCurve(const CcdDisplacement &)`: atoms sorted by minimum-image distance r_i from the centre;
     returns (r_i, cumulative w / Σw) pairs, last value 1.
   - `AtomsCarrying(const CcdDisplacement &, double fraction)`: smallest number of atoms (sorted by w_i descending)
     whose w sum ≥ fraction · Σw.
   - `DisplacementOverlap(const CcdDisplacement &a, const CcdDisplacement &b)`: nullopt unless same atom count and
     same symbol per atom; else |Σ m_i ΔR_a,i · ΔR_b,i| / (ΔQ_a ΔQ_b) using mass-weighted vectors (masses of `a`; if
     the masses differ per atom by > 1e-3 amu -> nullopt). No re-ordering or alignment of atoms (state this in the
     UI tooltip: same atom order required).
   - Mode coverage needs no new code: Σ_k q_k² over non-translation modes of the phonon result divided by ΔQ² of the
     same result (both already in `PhononAnalysisResult`); and the largest q_k²/ΔQ² share.
2. **Comparison table** (`PhononPlotData.{hpp,cpp}` `PhononComparisonRow` + `BuildPhononComparisonRows`,
   `PhononModesPanelComparison.cpp`): grow `values`/`reasons` with columns
   `ΔQ (CCD) [amu^½ Å]`, `max |ΔR| [Å]`, `atomy 90 % ΔQ²`, `pokrycie ΔQ² modami [%]`, `największy udział modu [%]`.
   The CCD columns come from the case's selected CCD dataset with a displacement (reason when missing: „Brak
   zestawu CCD z ΔR w przypadku.” / „Zestaw CCD bez ΔR - przelicz CCD.”); the mode columns from the phonon result
   (reason as the existing mode columns). The 90 % fraction uses `Settings().carriedFraction` (already persisted;
   header shows the actual percentage). Extend `PhononComparisonColumns` (TSV export) in the same order.
3. **Localisation plot**: a new plot in the comparison tab (or a sub-tab `Lokalizacja ΔR`) built by a new adapter
   `BuildLocalisationPlotData(session, project, roots, colors)` -> `PlotData` id `localisation`, x = r [Å],
   y = cumulative ΔQ² share, one step/line series per compared case with a displacement (series label = case name,
   `source` = CCD dataset dir), style keys like the other phonon plots (legend popup, export popup, figure button
   exactly as `S`/`PL` plots use them).
4. **Overlap matrix**: under the table, `Podobieństwo ΔR` - a small table of the compared cases with a
   displacement; cell = overlap as a number in [0, 1] with 3 decimals, „—” with a tooltip reason when not comparable
   („Różna liczba atomów lub kolejność pierwiastków.”). Exported with the same export popup as a separate table.
5. Per-case data is loaded lazily and cached by dataset path + mtime the way 109d caches compared results (search
   how `BuildPhononComparisonRows` gets results of non-active cases and reuse that cache; no second cache system).

## Files to create or change

- new `src/Domain/Vibrations/DisplacementAnalysis.{hpp,cpp}`
- `src/Presentation/Panels/PhononPlotData.{hpp,cpp}`, `PhononModesPanelComparison.cpp`, session files only to
  expose the per-case CCD displacement through the existing comparison cache
- tests: new `tests/Domain/Vibrations/DisplacementAnalysisTests.cpp`, the comparison/plot-data tests (search
  `BuildPhononComparisonRows` in `tests/`)

## Files that must NOT be touched

- Python scripts, IO (108b already loads the displacement), renderer, structure-building files, `Vendor/`,
  `install/`, `build/`; `docs/` except `docs/work/project/reports/task-108e-displacement-comparison.md`

## Acceptance criteria

1. Domain: centre is correct for a defect straddling the cell boundary; curve ends at 1 and is non-decreasing;
   `AtomsCarrying` on a 3-atom fixture; overlap = 1 for identical, ~0 for orthogonal, nullopt for different
   symbols/counts; scaling ΔR by a factor leaves overlap unchanged.
2. Rows: a case without CCD displacement shows the reasons; with one, the five columns are finite; TSV columns
   match the table order.
3. Plot adapter: one series per compared case with a displacement, x sorted, y ends at 1.
4. Existing tests pass. Report: files, tests, anything not done.

## Constraints

- Domain stays pure (no IO/UI includes); adapters in Presentation without ImGui.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
