# Task 111: Figure seed - FigureSpec JSON + matplotlib renderer, phonon S/PL pilot

Plans: `docs/work/project/plans/2026-10-11-figure-module.md` (approved direction, phase 1 seed) and
`docs/work/project/plans/2026-10-13-phonon-workspace.md`. Needs task 110 (`Presentation/Figures/PlotData.hpp` and the
phonon adapters). User 2026-10-13: "zalążek systemu do systematycznego generowania obrazków w matplotlib".

## Goal

From a phonon plot the user saves a figure for the thesis: a reproducible spec file next to the project
(`figures/<name>.figure.json`, data snapshot + resolved style) rendered by matplotlib to PDF/SVG/PNG in the
background. `Renderuj wszystkie` re-renders every spec of the project (one shared style file), and the same script
runs from the command line. Pilot: `S_k i S(ℏω)` and `PL`; the other phonon tabs export too but may lack the
custom decorations (CCD arrows/brackets) - listed as a known gap, not silently dropped.

## Design (binding)

1. **FigureSpec** (`src/Presentation/Figures/FigureSpec.{hpp,cpp}`, no ImGui/ImPlot):
   ```cpp
   struct FigureSpec {
       int schemaVersion = 1;
       std::string name;                    // file stem
       double widthCm = 8.5, heightCm = 6.5;
       std::string style = "thesis";        // figures/<style>.mplstyle next to the specs; created on first use
       std::vector<std::string> formats{"pdf"}; // pdf | svg | png
       int dpi = 300;                       // png and rasterised parts
       PlotData plot;                       // snapshot from the 110 adapters (series, axes, styles, sources)
       std::string createdBy;               // "DefectStudio <git describe or version>"
   };
   [[nodiscard]] std::string SerializeFigureSpec(const FigureSpec &spec);                  // JSON, NaN -> null
   [[nodiscard]] bool ParseFigureSpec(const std::string &json, FigureSpec &out, std::string &outError);
   ```
   JSON keys mirror the struct; series: `{id, label, kind, x, y, yAxis, source, style:{color:[r,g,b,a]|null,
   weight, visible, lineStyle, filled, fillAlpha}}`; axes `{labelTex, unit, minimum, maximum, log}`. Invisible series
   are kept with `visible:false` (the spec is a snapshot of the view, the renderer skips them).
2. **Renderer** `scripts/python/examples/render_figure.py` (matplotlib Agg, no app imports):
   `python render_figure.py <spec.json> [--out-dir DIR] [--formats pdf,png]` and `--all <figures dir>`.
   - Validates `schemaVersion == 1`, writes each format next to the spec (or `--out-dir`) via a temp file + rename,
     never leaving a broken file on error; prints one JSON line `{"ok": true, "outputs": [...]}` or
     `{"ok": false, "error": "..."}` (same contract as the other example scripts, `ExtractJsonLineFromOutput`).
   - Size in cm -> inches; `style` file `figures/<style>.mplstyle` applied when present, else a built-in `thesis`
     dict (white background, 8 pt fonts, mathtext, 0.8 pt axes, inward ticks); `labelTex` rendered as mathtext
     (`$...$` added around TeX-lite: `\hbar\omega` etc. map 1:1; units in brackets).
   - Kinds: Line -> plot; Stems -> vlines + markerless; Scatter -> scatter; VerticalLine -> axvline;
     Segments -> plot with NaN breaks; yAxis 1 -> twinx; legend from labels of visible series; null colour ->
     matplotlib cycle; lineStyle 0..3 -> solid/dashed/dotted/dashdot; filled -> fill_between to 0 with fillAlpha.
   - A tiny `--self-test` that renders a two-series spec to a temp dir and asserts the files exist (the "one runnable
     check" for the script).
3. **Job**: reuse `PythonScriptJob` (`ScientificRuntime/Python/PythonScriptJob.hpp`) with
   `ResolvePythonExampleScript("render_figure.py")`; the spec goes through the FILE, only paths on the command line.
   Main thread reads the result on completion (poll in the phonon session `Update`, like the other jobs).
4. **UI** (Plots view, next to 110's `Eksport…`): `Rycina…` popup - `Nazwa` (default `<plot id>_<label>`),
   `Szerokość [cm]` / `Wysokość [cm]` DragFloat (presets `8.5 cm` / `16 cm` buttons), formats checkboxes, `Zapisz i
   renderuj` (writes `figures/<name>.figure.json` in the PROJECT directory, then renders), status line with the
   project-relative output path, `Otwórz folder figures` button. A separate `Renderuj wszystkie` button (tooltip:
   re-renders every figures/*.figure.json with the current style file) runs `--all`. Disabled with a reason without
   a project.
5. **Existing synchronous electronic-structure image export is NOT migrated in this task** (plan phase 1 later).

## Files to create or change

- new `src/Presentation/Figures/FigureSpec.{hpp,cpp}`, new `scripts/python/examples/render_figure.py`
- Plots view files from 110 (`PhononModesPanelPlots.cpp` etc.), `PhononModesPanel.hpp`, phonon session job polling
  (`PhononPanelSession*.cpp/.hpp`: one more job slot) - keep files < 500 lines (new `PhononModesPanelFigure.cpp` OK)
- tests: new `tests/Presentation/Figures/FigureSpecTests.cpp` (roundtrip, NaN -> null -> NaN, invalid version);
  a test that runs `render_figure.py --self-test` through `PythonScriptJob`/`ScriptRunner` if such script tests exist
  in the repo (search; follow their skip rules when Python is unavailable)

## Files that must NOT be touched

- `src/Domain/*`, `src/IO/*` except reusing `TextFileIO`; structure-building files (another task); other panels
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-111-figure-seed.md`

## Acceptance criteria

1. FigureSpec JSON roundtrip of a two-series plot with a right axis and a NaN gap; schema version 2 rejected.
2. `.venv/Scripts/python.exe scripts/python/examples/render_figure.py --self-test` exits 0 and prints ok JSON.
3. Rendering a spec written by the app produces a PDF whose size in points equals widthCm/heightCm (report the check).
4. Existing tests pass. Report: files, tests, known gaps (CCD decorations), anything not done.

## Constraints

- Python draws, never recomputes physics. No new Python dependency (matplotlib 3.10 is in the bundled runtime).
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
