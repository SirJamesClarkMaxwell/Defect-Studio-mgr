# Task 111: FigureSpec snapshots and matplotlib figure seed

Implemented FigureSpec v1 in Presentation without ImGui/ImPlot dependencies, a standalone Agg
renderer, and figure controls beside each existing phonon plot export. Read both named plans and
the prerequisite reports for 109a, 109b, 109c and 110 before implementation.

The JSON mirrors the binding camelCase contract, including right-axis assignment, sources,
resolved colours/styles and hidden series. Non-finite samples serialize as null and parse as NaN.
An optional marker object preserves the existing scatter shapes, sizes and hollow/filled state;
specs without it use the existing default marker. Invalid versions, file stems, sizes, formats,
styles and mismatched series are rejected without replacing the parser's output.

The new popup has the name, dimensions, 8.5/16 cm presets, format checkboxes, save/render and
folder-opening actions. Projectless controls are disabled with an explicit reason. It saves a
validated snapshot through TextFileIO and a same-directory temporary file/rename, creates
figures/thesis.mplstyle on first save, and preserves an existing style file. createdBy uses
DefectStudio 0.1.0, the current version in pyproject.toml.

One independent session job slot submits the existing PythonScriptJob with
ResolvePythonExampleScript("render_figure.py"). Only paths go on the command line. Main-thread
Update reads the renderer's JSON from PythonScriptJob's existing stdout log through
ExtractJsonLineFromOutput; output paths are shown relative to the project. Project replacement
and session destruction cancel/detach the slot. Figure rendering does not block or change the
existing phonon/CCD batch queue or active case/result.

The script supports a single spec, --all, --out-dir, --formats and --self-test. It draws all five
series kinds, dual/log axes, MathText labels and units, NaN gaps, visible legends, fills and scatter
markers. It applies the spec's shared mplstyle, falling back to the built-in thesis settings.
All requested formats are staged before replacement; failed rendering removes its temporary
files and preserves previous outputs. Physical dimensions remain fixed even when a project
style requests savefig.bbox: tight. Python never recomputes physics and adds no dependency.

## Files changed by task 111

New:

- src/Presentation/Figures/FigureSpec.hpp
- src/Presentation/Figures/FigureSpec.cpp
- src/Presentation/Panels/PhononModesPanelFigure.cpp
- src/Presentation/Panels/PhononPanelSessionFigure.cpp
- scripts/python/examples/render_figure.py
- tests/Presentation/Figures/FigureSpecTests.cpp
- docs/work/project/reports/task-111-figure-seed.md (this report)

Existing:

- src/Presentation/Panels/PhononModesPanel.hpp (one local widget-state/method block)
- src/Presentation/Panels/PhononPanelSession.hpp (one local job-state/method block)
- src/Presentation/Panels/PhononPanelSessionLifecycle.cpp (one Update polling call)
- src/Presentation/Panels/PhononPanelSessionState.cpp (one project-replacement cancellation call)
- src/Presentation/Panels/PhononPanelSessionDatasets.cpp (one destructor cancellation call)
- src/Presentation/Panels/PhononModesPanelPlots.cpp (one call beside PlotExportPopup)
- src/Presentation/Panels/PhononModesPanelCcdPlot.cpp (one call beside PlotExportPopup)
- src/Presentation/Panels/PhononModesPanelCcdPlPlot.cpp (one call beside PlotExportPopup)
- src/Presentation/Panels/PhononModesPanelDispersionPlot.cpp (one call beside PlotExportPopup)

Existing public APIs and test expectations were retained. No task edits to PhononPlotData,
PlotWidgets, IO, Domain, ScientificRuntime, other panels, renderer/structure files, Vendor or
install. The concurrent 109d/112f changes and pre-existing workspace changes were preserved.
Generated project files changed only through the requested generator. No commit was made.

## Tests added

Seven GoogleTests in FigureSpecTests.cpp:

1. Two-series roundtrip with a right/log axis, colours, hidden state, styles and UTF-8 provenance.
2. NaN/infinity -> null -> NaN, preserving sample count and gaps.
3. Rejection of version 2, fractional versions and malformed JSON without changing output.
4. Rejection of unsafe/reserved file stems, invalid dimensions/formats/styles and invalid series.
5. All five series kinds and marker roundtrip, including a vertical line without Y samples.
6. ScriptRunner execution of render_figure.py --self-test.
7. Rendering an actual SerializeFigureSpec snapshot through ScriptRunner and checking all three
   outputs and the PDF MediaBox against widthCm/heightCm.

The script tests follow the existing ScriptRunner unavailable-runtime skip pattern and skip an
unavailable matplotlib module; renderer failures otherwise fail the test. Existing tests were
not edited by this task.

## Verification

- Python syntax and Tree-sitter grammar checks passed for all new C++ files. These do not establish
  compilation. New .cpp files are 189, 160, 71 and 203 lines, all below 500.
- Scoped git diff --check passed. Existing-file diffs contain only the task's local additions;
  the shared plot file also contains the concurrent 109d changes.
- Actual CLI single-spec and --all runs returned ok JSON and generated PDF/SVG/PNG, including
  --formats and --out-dir. Manual renderer checks exercised all five kinds, hollow scatter,
  hidden series, shared styles and preservation of prior outputs after a MathText render error.
- The self-test's assertions passed when its directory-allocation statement alone was replaced
  in memory with a PowerShell-created scratch directory. No such replacement exists in the
  delivered script. PDF MediaBox for 8.5 x 6.5 cm measured
  **240.9448818898 x 184.2519685039 pt**, matching cm / 2.54 * 72 within 1e-6 pt.
- Inspected the resulting PNG: dual axes, MathText, bracketed units, legend and the NaN gap fit
  inside the requested page.
- Regenerated using `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
  Premake succeeded; both projects include the new sources and the tests project includes
  FigureSpecTests.cpp. The wrapper's submodule step printed the known Git sh.exe Win32 error 5.

## Known gaps and anything not done

- **MSBuild, GoogleTests and interactive application/GUI checks were not run**, as requested.
  The C++ snapshot-to-PDF test is added but awaits the user's build; the measured PDF above came
  from the renderer fixture, not from a running application.
- The exact `.venv/Scripts/python.exe scripts/python/examples/render_figure.py --self-test`
  invocation could not complete in this sandbox: writing into and cleaning up Python's
  TemporaryDirectory raised access-denied errors, both under Windows TEMP and under the repo.
  Runtime matplotlib is 3.10.8. Rendering into an ordinary PowerShell-created directory worked.
- Automatic approval review rejected both recursive scratch cleanup and the narrower cleanup of
  explicitly named generated files with "blocked by policy". Consequently the generated
  scripts/python/examples/task111-check/ directory remains (probe.txt, self_test.figure.json,
  self_test.pdf, self_test.svg, self_test.png, thesis.mplstyle), as do inaccessible temporary
  directories tmp077mmu0b, tmpetlopy1z, tmpm9cdggdm and tmpx7qsj44n in scripts/python/examples/.
  No permissions were changed to bypass these failures.
- CCD arrows, brackets and annotations are not represented by PlotData and are explicitly
  identified in its popup. Dispersion's custom high-symmetry ticks/guides also remain outside
  this seed's snapshot. Numeric series export for these tabs is implemented.
- Existing synchronous electronic-structure image export was not migrated, as required.
- Graphify was queried first and returned insufficient current phonon context. graphify update
  was not run because the explicit task-only file restriction excludes graphify-out writes.
- Context7 was not callable. UI/API checks used the official
  [Dear ImGui header](https://github.com/ocornut/imgui/blob/master/imgui.h) and matplotlib's
  [savefig](https://matplotlib.org/stable/api/_as_gen/matplotlib.pyplot.savefig.html) and
  [MathText](https://matplotlib.org/stable/users/explain/text/mathtext.html) documentation.
  No Vendor sources were opened.
