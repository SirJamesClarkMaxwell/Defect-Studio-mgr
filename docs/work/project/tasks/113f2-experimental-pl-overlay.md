# Task 113f2: experimental PL spectra in the Phonons plots and comparison

Plan: `docs/work/project/plans/2026-11-17-phonon-server-results-and-comparison.md`. Builds on 113f1 (done:
`ParseDataTable`/`LoadDataTable` in `src/IO/DataTableIO.hpp`, `ExperimentalSpectrum` + `SpectrumAxisUnit` +
`SpectrumPeakEnergyEv` in `src/Domain/Vibrations/ExperimentalSpectrum.hpp`) and 113e (hidden cases, comparison of
all visible cases). User 2026-11-17: load an experimental PL spectrum from a file and compare it with the computed
ones; text files with energy in eV now, more formats later (the unit enum is the extension point).

## Goal

Each case can hold any number of experimental spectra read in place from text files. They are drawn on the PL plot
of the case and in the defect comparison with the same axis (E or E_ZPL − E) and the same normalisation
(none/max/area) as the computed spectra, have legend popups, and go into the TSV/figure exports.

## Design (binding)

1. **Settings**: `PhononCase` gains `std::vector<ExperimentalSpectrumSource> experimentalSpectra` with
   `{Path file; int xColumn = 0; int yColumn = 1; std::string unit = "eV"; std::string label; std::optional<double>
   zplEv; bool visible = true;}` (declare the struct in `PhononPanelSettingsIO.hpp`); yaml per case key
   `experimental: [{file, x, y, unit, label, zpl, visible}]`, file relative to the project when inside it (same rule
   as other paths), formatVersion stays 2, unknown unit -> warning + skipped entry; round-trip test.
2. **Loading**: the session keeps a cache `file + mtime + columns + unit -> ExperimentalSpectrum` (or error text);
   files are read on the main thread only if small (< 5 MB), else through a job - pick one approach and justify it
   in the report. Unit string -> `SpectrumAxisUnit` in exactly one function next to the settings IO (the place to
   add „nm” / „cm-1” later).
3. **UI** in `Fonony` (case inputs, section „PL eksperymentalne”): „Wczytaj widmo…” (file dialog, filters
   *.txt *.dat *.csv *.tsv and all files) -> a small popup showing the first 8 lines of the file, the detected
   delimiter, number of columns and skipped lines, combos for the x and y column (names from the header when
   present), unit combo (only „eV” now; tooltip: „Inne jednostki - wkrótce.”), label (default: file name), then
   „Dodaj”. Errors from `ParseDataTable`/`BuildExperimentalSpectrum` are shown in the popup, „Dodaj” disabled with
   the reason. List of the case's spectra: visibility checkbox, label, file (relative, tooltip full), ZPL [eV]
   DragFloat (empty = none) with a button „z maksimum” that runs `SpectrumPeakEnergyEv` in a window
   [ZPL computed − 0.15, + 0.15] eV around the case's current PL ZPL (tooltip says the window) and „x” to remove.
4. **Plots** (through the existing data adapters of task 110 - `PhononPlotData*`, no direct ImPlot-only drawing):
   - PL plot of the active case: each visible spectrum as a line series „<label> (eksp.)”, style key
     `pl/exp/<case id>/<index>` so the legend popup and persistence work like other series;
   - axis E: x = energy; axis E_ZPL − E: x = (zpl − E)·1000 meV with zpl = the spectrum's `zplEv`, else the
     ZPL the case's computed curve uses (state which one in the series tooltip/comment); a spectrum with neither
     is skipped on that axis with a reason in the legend tooltip;
   - normalisation: the same function the computed curves use (max / area over the full measured range);
   - comparison PL (`Porównanie`): the visible spectra of every compared, visible case, colour of the case,
     dashed or thinner line;
   - TSV export and the figure spec include the experimental series (they come from the adapters).
5. Comparison table: column „ZPL eksp. [eV]” = the first visible spectrum's `zplEv` (or „—”).

## Files to create or change

- `src/IO/PhononPanelSettingsIO*.{hpp,cpp}`, `src/Presentation/Panels/PhononPanelSession*.{hpp,cpp}` (new
  `PhononPanelSessionExperimental.cpp`), `PhononModesPanel*.cpp` (new `PhononModesPanelExperimental.cpp` for the
  section + popup), `PhononPlotData*.{hpp,cpp}`, tests for settings round trip and the plot adapters
- `docs/work/project/reports/task-113f2-experimental-pl.md`

## Files that must NOT be touched

- `src/IO/DataTableIO.*`, `src/Domain/Vibrations/ExperimentalSpectrum.*` (use them; if something is missing,
  document it instead of changing them), `scripts/`, `Vendor/`, `install/`, `build/`, renderer, energetics files

## Acceptance criteria

1. Settings round trip incl. relative file path and unknown unit warning; old files load unchanged.
2. Adapter tests: the experimental series appears with the right x on both axes, normalised as the computed ones,
   is skipped (with a reason) when no ZPL is known on the E_ZPL − E axis, and hidden cases contribute nothing.
3. Existing tests pass (I build and run). Report with the exact click path for the user.

## Constraints

- Layers: IO no UI; adapters without ImGui; only the main thread commits session state.
- `.cpp` files under ~500 lines. UI rules: paths relative to the project, labels left/above, tooltips everywhere,
  disabled buttons say why, DragFloat for continuous values.
- STYLE RULE: keep existing formatting of edited files.
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
- If a binding detail conflicts with existing code, keep the user-visible behaviour, document the deviation and continue.
