# Task 113f2 — experimental PL overlay

Implemented per-case experimental PL sources, import preview, asynchronous loading, shared PL adapters,
comparison, and TSV/figure snapshots. Source files are read in place and are never rewritten or copied into
the project. Schema version remains 2. No commit was made.

All file sizes go through the existing JobSystem. This avoids a second synchronous loading path and keeps
file reading, table parsing, sorting and spectrum construction off the UI thread, including files over 5 MB.
The session cache is keyed by resolved file path, x/y columns and unit, and invalidated by mtime. It retains
parsed preview metadata, the spectrum or its error. Job results are moved into session state only by a
main-thread poll; project reset/destruction cancels outstanding requests and discards their results.

The existing computed-PL normalization was extracted unchanged into `NormalizePhononPl`; both computed and
experimental adapters call it. Area is integrated over the full input grid in the displayed axis units, so
unit-area densities on the loss axis have units meV^-1. Experimental data retain their entire measured range.
The loss axis uses the spectrum's explicit ZPL first, then the ZPL selected for that case's computed curve
with the shared PL selector (including its existing fallback). The legend tooltip names that source.
Experimental lines inherit the case's computed-series colour and default to a thinner dashed style.

Files changed for this task (changes belonging to the concurrent 113d session are excluded):

- `src/IO/PhononPanelSettingsIO.hpp`
- `src/IO/PhononPanelSettingsIOCases.hpp`
- `src/IO/PhononPanelSettingsIOCases.cpp`
- `src/IO/PhononPanelSettingsIOExperimental.cpp` (new)
- `src/Presentation/Panels/PhononPanelSession.hpp`
- `src/Presentation/Panels/PhononPanelSessionExperimental.cpp` (new)
- `src/Presentation/Panels/PhononPanelSessionCases.cpp`
- `src/Presentation/Panels/PhononPanelSessionDatasets.cpp`
- `src/Presentation/Panels/PhononPanelSessionLifecycle.cpp`
- `src/Presentation/Panels/PhononPanelSessionState.cpp`
- `src/Presentation/Panels/PhononModesPanelExperimental.cpp` (new)
- `src/Presentation/Panels/PhononModesPanelInputs.cpp`
- `src/Presentation/Panels/PhononModesPanelPlots.cpp`
- `src/Presentation/Panels/PhononModesPanelComparison.cpp`
- `src/Presentation/Panels/PhononPlotData.hpp`
- `src/Presentation/Panels/PhononPlotData.cpp`
- `src/Presentation/Panels/PhononPlotDataExperimental.cpp` (new)
- `src/Presentation/Panels/PhononPlotDataComparison.cpp`
- `tests/IO/PhononPanelSettingsIOExperimentalTests.cpp` (new)
- `tests/Presentation/Panels/PhononPlotDataExperimentalTests.cpp` (new)
- `tests/Presentation/Panels/PhononComparisonTests.cpp`
- This report.

Integration decisions/deviations from a literal reading of the binding:

- `PlotSeriesData` has no tooltip/error field. Missing-ZPL or failed/pending loads use the existing empty
  line/legend-dummy behaviour; no data curve is drawn, and `PhononExperimentalSeriesNote` supplies the legend
  tooltip. This preserves the requested explanation without changing the common figure types outside scope.
- An optional ZPL uses the existing checkbox-plus-DragFloat pattern: unchecked means absent, shown as “—”.
  “z maksimum” is disabled with a reason if the computed ZPL, loaded spectrum or samples in the window are
  unavailable. Its tooltip gives the actual window around the current computed PL ZPL.
- The preview uses `TextFileIO::Load` followed by `ParseDataTable`, rather than `LoadDataTable`, so the first
  eight source lines and parsed data come from one read. Errors from both parsing and spectrum construction
  disable “Dodaj”. Unit-string conversion exists only in `ExperimentalSpectrumUnit`, beside settings IO.
- The one-argument comparison export API keeps its existing twelve-column contract. The full table/export
  overload gains the 23rd column, “ZPL eksp. [eV]”; the two existing size assertions were updated accordingly.
- The experimental UI is a free section function, following the existing case/defect-folder pattern, so
  `PhononModesPanel.hpp` did not need an edit from this task. Case duplication also copies measured sources.
- Work stayed on the existing branch because another session is actively editing the same working tree.

Exact click path after building:

1. Open a project, open **Fonony**, and select the case in its case list.
2. Expand **PL eksperymentalne** → **Wczytaj widmo…**. Choose a `.txt`, `.dat`, `.csv` or `.tsv` file (the
   native dialog also offers all files).
3. In **Import PL eksperymentalnego**, inspect **Pierwsze 8 linii**, separator, column count and skipped-line
   count. Select **Kolumna x**, **Kolumna y**, **Jednostka: eV**, and **Etykieta**, then click **Dodaj**.
4. In the case's measured-spectrum list, use the visibility checkbox, optional **ZPL [eV]**, **z maksimum**
   (computed ZPL ± 0.15 eV), or **x** to detach the source. File labels are relative to the project; hover
   for the full source path.
5. Open the phonon plots window → **PL**. Select **Oś** (`E` / `E_ZPL − E`), **Normalizacja**
   (`brak` / `max = 1` / `pole = 1`) and the computed **ZPL** curve. Measured legends end with **(eksp.)**.
   Right-click a legend entry for the existing style popup; hover for the ZPL source or skip/error reason.
6. Open **Porównanie** → **Porównaj wszystkie widoczne**, or select individual datasets. The new experimental
   ZPL table column and **PL — porównanie** plot use the visible compared cases and the active visible case.
   The comparison PL plot shares the controls in the **PL** tab.
7. Use the plot's **Eksport TSV…** / clipboard or **Rycina…** → **Zapisz i renderuj**. Both consume the same
   adapter data, including measured series and their style/source. Reopen the project to check persistence.

Validation:

- Added 12 C++ tests: three settings tests (multiple sources, relative/external paths, optional ZPL,
  styles, unsupported units/malformed entries, legacy defaults) and nine adapter/session tests (both axes,
  selected-computed fallback, full-range max/area normalization, missing-ZPL legend explanation, standalone
  measurements, case colours/visibility, duplication, first-visible experimental ZPL, TSV/figure round trip,
  column/mtime/error caching, peak window, project reset and main-thread publication).
- C++ compilation and test execution were deliberately not run; MSBuild was not invoked. UI smoke testing
  and actual PDF/SVG/PNG rendering remain for the user after building. Suggested filter:
  `PhononPanelSettingsIOExperimentalTests.*:PhononPlotDataExperimentalTests.*:PhononPlotDataTests.*:PhononComparisonTests.*`.
- Ran `.venv/Scripts/python.exe -m pytest scripts/python/tests -q`: **111 passed, 5 failed**, 21 subtests
  passed. Repeated with `TEMP`/`TMP` inside `.task-113f2-temp` in the repository and pytest's cache provider
  disabled: **113 passed, 5 failed**, 21 subtests passed. The concurrent session added tests between runs.
  Four failures are `test_check_include_directions.py` tests denied access while creating child directories
  in their temporary fixtures. `test_ccd_inputs.py::CcdInputsTests::test_dimer_ccd_points` also fails: its
  subprocess exits 1 after the MAGMOM warning, and fixture cleanup raises WinError 5. Moving temporary files
  inside the repository did not fix these permission failures. No Python sources were edited by this task.
- Ran the required project regeneration with `DS_TOOLSET=msc-v143`; exit code 0 and premake reported
  completion. Git's helper printed a CreateFileMapping/Win32 error 5 before generation, alongside existing
  premake deprecation warnings. Verified all four new runtime translation units and both new test files
  occur in the generated projects.
- The edited/new runtime `.cpp` files remain below approximately 500 lines; the largest is
  `PhononPlotData.cpp` at 489 lines. Scoped `git diff --check` passes.

Graph maintenance: `graphify update .` failed with exit code 1: `[graphify watch] Rebuild failed:
[WinError 5] Odmowa dostępu` (access denied). The existing graph could not be refreshed in this environment.

Temporary test fixtures: automatic approval review rejected cleanup of `.task-113f2-temp` with
“blocked by policy”, including a second attempt restricted to empty directories. Its five failed-test
temporary directories remain. No cleanup was attempted against another session's temporary directories.

Remaining risk: the C++ changes require the user's build/test and UI verification. The five Python failures
above remain unresolved and are outside the files authorized for this task. Concurrent 113d edits were
preserved; shared-file changes from this task are local additions only.
