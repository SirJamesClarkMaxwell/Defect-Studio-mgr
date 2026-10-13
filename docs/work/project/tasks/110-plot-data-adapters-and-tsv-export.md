# Task 110: plot data adapters + TSV/CSV export of the phonon plots

Plan: `docs/work/project/plans/2026-10-13-phonon-workspace.md` (task 110) and the Figure plan
`docs/work/project/plans/2026-10-11-figure-module.md` ("one preparation function supplies the numbers to ImPlot,
TSV and the FigureSpec"). User 2026-10-13: "Czy mamy możliwość do eksportu danych do tsv/csv?"

## Goal

Every phonon plot (`Fonony: wykresy`: β, S_k i S(ℏω), PL, CCD, PL 1D, Dyspersja) can be exported as the data it
shows: all visible series after the transformations the plot applies (ZPL choice, normalisation, comparison
overlays), as TSV, CSV or clipboard text for Origin. The modes table exports to TSV. The same plot description is
the input of the matplotlib Figure task (111), so it must carry labels, units, styles and axis ids.

## Design (binding)

1. **Plot description, UI-free** (`src/Presentation/Figures/PlotData.hpp`, header-only or + `.cpp`):
   ```cpp
   enum class PlotSeriesKind { Line, Stems, Scatter, VerticalLine, Segments }; // Segments: NaN breaks the path
   struct PlotSeriesData {
       std::string id;          // stable within the plot ("S/<dataset label>", "A1", "chi_gs_0", ...)
       std::string label;       // legend text
       PlotSeriesKind kind = PlotSeriesKind::Line;
       std::vector<double> x, y;
       int yAxis = 0;           // 0 = left, 1 = right (S_k vs S(hw))
       Path source;             // dataset directory the numbers come from (empty = computed in the panel)
       PlotSeriesStyle style;   // resolved style (IO/PlotSeriesStyle.hpp), visible=false series stay in the data
       PlotMarker marker;       // Scatter only (amended 2026-10-14)
   };
   // Marker look of a Scatter series. Points that differ in shape/fill (CCD gs/es circles vs triplet diamonds,
   // converged filled vs not hollow) become separate series - no per-point style.
   struct PlotMarker { enum class Shape { Circle, Diamond, Square }; Shape shape = Shape::Circle; float size = 5.0f;
                       bool filled = true; };
   struct PlotAxisData { std::string labelTex; std::string unit; double minimum = 0, maximum = 0; bool log = false; };
   struct PlotData { std::string id; std::string title; PlotAxisData x, y, y2; bool hasY2 = false;
                     std::vector<PlotSeriesData> series; };
   ```
   No ImGui/ImPlot include. Lives in Presentation (it is a view model, not Domain).
2. **Adapters** (`src/Presentation/Panels/PhononPlotData.{hpp,cpp}`, split if > 500 lines): one function per plot
   building `PlotData` from the session state the plot uses today - beta/S stems of the visible (or compared)
   modes, selected-mode line, S(ℏω) and PL curves of the active + compared datasets (same `GridIndexRange`
   trimming and ZPL curve choice as now), CCD curves/points/levels/wavefunctions, PL 1D, dispersion branches
   (one Segments series per branch; path breaks as NaN). Custom draw-list decorations (CCD arrows/brackets,
   TeX labels) are NOT series; leave them where they are (task 111 adds annotations).
3. **Drawing goes through the adapters**: each plot tab builds its `PlotData` once per frame and draws the series
   with a small generic helper in `PlotWidgets` (`PlotSeries(const PlotSeriesData&, PlotSeriesStyle &style)` that
   dispatches to the existing `PlotStyledLine/Stems/InfLine` and keeps `SeriesLegendPopup`). Interaction code
   (mode picking by click, CCD hover) stays as is. Visual result must be unchanged.
4. **Table writer in IO** (`src/IO/DataTableIO.{hpp,cpp}`) - ALREADY IMPLEMENTED AND TESTED by Claude (commit after
   7f4ab9f, `tests/IO/DataTableIOTests.cpp`); use it, do not change it unless a test proves a bug. Contract:
   ```cpp
   // Numeric column (values, NaN -> empty cell) or text column (text non-empty; e.g. irreps) - amended 2026-10-14.
   struct DataTableColumn { std::string name, unit, comment; std::vector<double> values; std::vector<std::string> text; };
   struct DataTableFormat { char delimiter = '\t'; bool originHeader = false; int significantDigits = 9; };
   // Header: one line of names; originHeader adds "Units" and "Comments" lines (Origin: Long Name / Units / Comments).
   // Columns of unequal length are padded with empty cells. Header and text cells are quoted when they contain the
   // delimiter, a quote or a newline (RFC 4180 for CSV; for TSV a tab/newline inside a text cell becomes a space). UTF-8, '\n' line ends, '.' decimal point always.
   [[nodiscard]] std::string FormatDataTable(std::span<const DataTableColumn> columns, const DataTableFormat &format);
   [[nodiscard]] bool SaveDataTable(const Path &file, std::span<const DataTableColumn> columns,
       const DataTableFormat &format, std::string &outError); // via TextFileIO (atomic), as other IO writers
   ```
   Presentation converts `PlotData` -> columns: an X/Y pair per series (`<label> x`, `<label> y`, unit from the
   axis, comment = project-relative source path), stems export (x, height), a vertical line one X value.
5. **UI**: in each plot tab's control row a small `Eksport…` button opening a popup: `Zapisz TSV…`, `Zapisz CSV…`
   (both through `PickSaveFile` in `Core/Platform/FileDialog`), `Kopiuj do schowka (TSV)` (`ImGui::SetClipboardText`),
   checkbox `nagłówek Origin` (tooltip: dodatkowe wiersze Units i Comments), checkbox `tylko widoczne serie`
   (default on). Default file name `<plot id>_<active case or dataset label>.tsv` in the project directory.
   Result line under the button: `Zapisano N kolumn: <project-relative path>` or the error.
   Modes view: `Eksport tabeli…` next to the filters -> TSV/CSV of the visible modes with all table columns.

## Files to create or change

- new `src/Presentation/Figures/PlotData.hpp`, new `src/Presentation/Panels/PhononPlotData.{hpp,cpp}`
- (`src/IO/DataTableIO.{hpp,cpp}` exists - use it)
- `src/Presentation/Panels/PlotWidgets.{hpp,cpp}` (generic series draw helper + export popup helper)
- `src/Presentation/Panels/PhononModesPanelPlots.cpp`, `PhononModesPanelCcdPlot.cpp`, `PhononModesPanelCcdPlPlot.cpp`,
  `PhononModesPanelDispersionPlot.cpp`, `PhononModesPanelView.cpp` (table export), `PhononModesPanel.hpp`
- tests: new `tests/IO/DataTableIOTests.cpp`, new `tests/Presentation/Panels/PhononPlotDataTests.cpp`

## Files that must NOT be touched

- `src/Domain/*`, `src/ScientificRuntime/*`, `scripts/python/*`, other panels (energetics keeps its own exporter)
- structure-building files (another task): `src/Renderer/*`, `src/Presentation/Panels/Viewport*`, `CellPanel*`, wizard
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-110-plot-data-export.md`

## Acceptance criteria (GoogleTests)

1. (DataTableIO tests exist and must stay green.)
2. Adapter on a synthetic two-mode result: beta plot has `A1` and `inne irreps` stems with the right x/y; S plot has
   the S(ℏω) line on yAxis 1; PL plot uses the chosen ZPL curve; a compared dataset adds one series with its source.
3. Dispersion adapter: two branches -> two Segments series, NaN at path breaks preserved.
4. Conversion `PlotData` -> columns: X/Y pair per visible series, names from labels, units from axes.
5. All existing phonon tests still pass; report lists files, tests, anything not done.

## Constraints

- UI conventions: labels left/above, tooltips on every new control, disabled buttons say why, project-relative
  paths with the full path in a tooltip.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
