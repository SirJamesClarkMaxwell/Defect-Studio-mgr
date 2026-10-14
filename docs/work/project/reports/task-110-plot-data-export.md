# Task 110: phonon plot adapters and TSV/CSV export

Implemented UI-free `PlotData` snapshots for beta, S_k/S(hw), PL, CCD, PL 1D and dispersion.
Each plot prepares its numeric series once per frame and draws those series through `PlotSeries`.
The same snapshot supplies the export columns. Existing public APIs and existing tests were retained.

The adapters reuse the session's mode filters, selected mode, PL curve choice/fallback, useful ranges,
grid trimming and comparison caches. S(hw) uses the right Y axis. Comparison series retain their source
directories. Dispersion produces one Segments series per branch with NaN path breaks. Hidden series
remain in the snapshot. Labels are plain legend text; UI legend IDs and persisted style keys retain
their previous identities, including shared CCD/dispersion legends. Automatic legend colours remain
attached to retained ImPlot items when comparisons change.

CCD exports sampled wells, scatter groups separated by branch and marker fill, level endpoints, and
energy-offset wavefunctions or probabilities with the chosen scale. Missing point energies use the
same interpolated/orientational marker positions as before. Existing hover/picking interactions,
transition arrows, brackets, annotations, TeX labels and dispersion axis guides remain in the UI.

Every plot has an export popup with TSV, CSV, clipboard TSV, Origin headers and a default-on visible-only
filter. Saves use `PickSaveFile` and the existing `SaveDataTable`; clipboard uses `FormatDataTable`.
Default names use the plot ID and active case name (dataset label fallback), in the project directory.
Successful saves show the column count and project-relative path, with the full path in a tooltip;
dialog/writer failures show their error. Disabled export actions explain the absence of series.

The modes view exports all seven columns of filtered modes, including text irreps and N(X%), in the
current table sort order. Numeric columns use the existing nine-significant-digit writer. X/Y pairs
use axis units and source comments; a vertical line exports one X column. NaN breaks remain empty
cells through the existing writer.

## Files changed

New:

- `src/Presentation/Figures/PlotData.hpp`
- `src/Presentation/Panels/PhononPlotData.hpp`
- `src/Presentation/Panels/PhononPlotData.cpp`
- `tests/Presentation/Panels/PhononPlotDataTests.cpp`
- `docs/work/project/reports/task-110-plot-data-export.md` (this report)

Existing:

- `src/Presentation/Panels/PlotWidgets.hpp`
- `src/Presentation/Panels/PlotWidgets.cpp`
- `src/Presentation/Panels/PhononModesPanel.hpp` (one include and one local widget-state member block)
- `src/Presentation/Panels/PhononModesPanelPlots.cpp`
- `src/Presentation/Panels/PhononModesPanelCcdPlot.cpp`
- `src/Presentation/Panels/PhononModesPanelCcdPlPlot.cpp`
- `src/Presentation/Panels/PhononModesPanelDispersionPlot.cpp`
- `src/Presentation/Panels/PhononModesPanelView.cpp`

`src/IO/DataTableIO.{hpp,cpp}` and `tests/IO/DataTableIOTests.cpp` were used unchanged. No edits to
session files, task 109c/112e implementation files, Domain, ScientificRuntime, Python, other panels,
Vendor or install were made. Generated project files were produced only by the requested generator.
Existing formatting and unrelated concurrent changes were preserved. No commit was made.

## Tests added

Eleven GoogleTests in `PhononPlotDataTests.cpp` cover:

- Two-mode beta grouping, filters, selected-mode vertical line, hidden styles and palette resolution.
- S(hw) on Y2 and the existing grid trimming, including neighbour samples.
- Chosen PL curve and the existing unavailable-choice fallback.
- S/PL comparison series, provenance and compared-only PL without an active result.
- Two dispersion branches and NaN breaks preserved through table formatting.
- Column names, units, provenance, visibility and the single X column for a vertical line.
- CCD circle/diamond, filled/hollow groups and missing-energy projections.
- CCD levels, energy offsets, scaled probability curves and shared hidden-wave styles.
- All three PL 1D curves and the optional phonon PL overlay.
- Compared CCD Numerov data, its source and the combined plotting range.
- All modes-table columns, text irreps, carried-atom counts, filtering and descending sort order.

## Verification and anything not done

- Scoped `git diff --check` passed. All task `.cpp` files remain below 500 lines.
- Tree-sitter parsed 12 task C++ files with no new grammar findings. Its one remaining missing-type
  finding is the unchanged `DrawFolder(..., const Path &hint = {})` declaration in the existing panel
  header. This is a grammar check, not compilation.
- Regenerated projects with `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
  Premake succeeded; both generated projects include the new adapters/model, and the test project
  includes `PhononPlotDataTests.cpp`. The wrapper's submodule step printed the known Git `sh.exe`
  CreateFileMapping / Win32 error 5 sandbox failure.
- **MSBuild, GoogleTests and interactive GUI validation were not run**, as requested. Compilation,
  test execution and visual checks remain for the user's build.
- Read both named plans and the available prerequisite reports (104, 109a, 109b). Task 109d's shifted
  PL axis/normalisation and case-summary table are not present in this working tree; the snapshots
  export the transformations applied by the existing plots. No new 109d features were added.
- Graphify was queried before code navigation and did not surface the current phonon implementation.
  `graphify update .` was not run because the explicit allowed-file constraint excludes graphify-out.
- Context7 was not available. The new ImGui API usage was checked against the official
  [Dear ImGui header](https://github.com/ocornut/imgui/blob/master/imgui.h); ImPlot's local header was
  used as permitted by AGENTS.md.

FigureSpec/matplotlib rendering remains task 111; this task supplies its plot data model.
