# Task 113e — phonon defects from folders

Implemented folder proposals, case creation, remembered roots, server-result refresh badges, hide/show,
and comparison/export metadata. The scanner header and its supplied tests are unchanged.
C++ compilation and test execution remain for the user, as requested; no C++ passing result is claimed.

## Behaviour

- `ScanPhononDefectFolder` classifies runs, prunes the excluded directories, selects the deepest systems
  containing GS + ES, ranks role candidates, and reuses `FindServerHrResults` / `FindDatasets`.
- Folder discovery and imported-summary/mtime checks extend `AnalysisDatasetScanJob`. Workers receive
  copied settings; `PollDatasetScan` commits on the main thread. Cancellation/project changes discard
  the previous job's results.
- The cases area offers `Dodaj defekt z folderu…`, remembered roots with `x`, rescan/cancel controls,
  and `Nowe przypadki z folderu`. Proposals have editable names, optional role selections, dataset/server
  checkboxes, and initially unchecked duplicate GS/ES pairs identified by case name.
- Case creation preserves the active case; an empty session activates the first new case. Bulk FC is
  reused only when existing cases identify one distinct nonempty directory. Only checked results are
  selected/attached/imported. Server imports use the existing batch queue and its validation/statuses.
- `defectRoots`, per-case `sourceDir`, and `hidden` are optional YAML keys; formatVersion remains 2.
  Existing project-load dataset scans also rescan remembered defect roots silently.
- New and updated HR results are compared with each case's imported `server.hr_dir` and Unix
  `server.hr_factors_mtime`. Badges expose the paths and mark updates as `zaktualizowane na serwerze`.
  Successful imports remove the corresponding badge entries; failed/cancelled entries remain available.
- Hidden cases stay dimmed at the bottom. Comparison, plot adapters, mode-table rows/exports, and batch
  computation skip them. Hidden CCD results stay cached; hidden dispersion views are cleared and
  restored from the existing selection when shown, avoiding the CCD renderer's assumption that its
  well series always exist.
- Comparison adds `Porównaj wszystkie widoczne`, `Wyczyść`, and columns for source, cell, HR method,
  cutoff, and E_FC = sum S_k * hbar omega_k [meV]. Full TSV order matches the UI. Missing values have
  reasons; translation modes do not contribute to E_FC. Visible cases without a result retain an empty row.

## Exact click path and server evidence

1. Open a local project and the `Fonony` window.
2. In `Przypadki`, click `Dodaj defekt z folderu…` beside `Nowy przypadek`; choose `P:/PL-spectra`.
   Alternatively, select folders in the project tree, right-click, and choose `Dodaj do analizy fononów`.
3. In `Nowe przypadki z folderu`, find `dimer/CH0/R2SCAN/9x9x1`. Its proposed picks are:
   GS `relax`, ES `excited/try_1_singlet`, triplet `excited/try_1_tryplet`, FC `dispersion`.
   Confirm/change the roles and checked server/app results, then click `Utwórz zaznaczone (N)`.
4. In `Fonony: wykresy` → `Porównanie`, click `Porównaj wszystkie widoczne`. Right-click a case to
   hide/show/remove it. Reopen the project or click `Sprawdź nowe wyniki` to refresh server badges;
   `Wczytaj` imports into that case without activating it.

Read-only checks of the actual mounted system confirmed both OUTCAR/CONTCAR files in the three
named runs, `dispersion/dephonopy_disp.yaml`, and **51 `hr_factors.dat` files** below `spectral-function`
(above the required 42+). No server file was written. The rebuilt application's popup/import workflow
has not been exercised in the GUI.

## Files changed by 113e

New files:

- `src/IO/PhononDefectFolderScan.cpp`
- `src/Presentation/Panels/PhononPanelSessionDefectFolders.cpp`
- `src/Presentation/Panels/PhononModesPanelDefectFolders.cpp`
- `tests/IO/PhononDefectFolderSettingsTests.cpp`
- `tests/Presentation/Panels/PhononDefectFolderSessionTests.cpp`
- `docs/work/project/reports/task-113e-phonon-defects-from-folder.md`

Updated files:

- `src/IO/PhononPanelSettingsIO.hpp`
- `src/IO/PhononPanelSettingsIO.cpp`
- `src/IO/PhononPanelSettingsIOCases.hpp`
- `src/IO/PhononPanelSettingsIOCases.cpp`
- `src/Events/ProjectEvents.hpp`
- `src/Presentation/EditorLayer.hpp`
- `src/Presentation/EditorLayerAnalysis.cpp`
- `src/Presentation/EditorLayerEvents.cpp`
- `src/Presentation/Panels/ProjectTreePanel.cpp`
- `src/Presentation/Panels/PhononPanelSession.hpp`
- `src/Presentation/Panels/PhononPanelSession.cpp`
- `src/Presentation/Panels/PhononPanelSessionCases.cpp`
- `src/Presentation/Panels/PhononPanelSessionDatasets.cpp`
- `src/Presentation/Panels/PhononPanelSessionServer.cpp`
- `src/Presentation/Panels/PhononPanelSessionState.cpp`
- `src/Presentation/Panels/PhononPanelSessionBatch.cpp`
- `src/Presentation/Panels/PhononPanelSessionCcd.cpp`
- `src/Presentation/Panels/PhononPanelSessionBulkMode.cpp`
- `src/Presentation/Panels/PhononPanelSessionJobs.cpp`
- `src/Presentation/Panels/PhononModesPanelCases.cpp`
- `src/Presentation/Panels/PhononModesPanelComparison.cpp`
- `src/Presentation/Panels/PhononPlotData.hpp`
- `src/Presentation/Panels/PhononPlotData.cpp`
- `src/Presentation/Panels/PhononPlotDataComparison.cpp`
- `tests/Presentation/Panels/PhononBatchTests.cpp`
- `tests/Presentation/Panels/PhononComparisonTests.cpp`

The shared settings, server queue, session header, and batch tests also contain concurrent 113b edits.
Those edits were preserved, including sigmaMaxMeV validation/forwarding. No scripts, vendor code,
energetics code, renderer code, or 113f files were edited by this session. Projects were regenerated
through the required command; generated project files include the new sources/tests.

## Validation

- `git diff --check`: passed. Protected scanner header/test diff: empty.
- Project generation with `DS_TOOLSET=msc-v143`: exit 0; VS2022 application/test projects regenerated.
  It emitted existing Premake deprecations and a Git shell CreateFileMapping access-denied diagnostic.
- C++ regression coverage added for YAML round trips/defaults/invalid optional fields; picked case
  creation, active-case preservation, bulk-FC ambiguity, checked datasets, background commits, restored
  roots, changed HR timestamps, cancelled project scans, hidden-case batch/CCD behaviour, duplicate
  proposal defaults, routing imports to two cases, comparison metadata/E_FC, missing results, and hiding.
- The existing full-comparison export test now expects 22 columns, as required by the five additions.
  The original 14 numeric values and legacy 12-column overload retain their tests and order.
- `.venv/Scripts/python.exe -m pytest scripts/python/tests -q`: **96 passed, 5 failed, 21 subtests passed**.
  A rerun with TEMP/TMP and the pytest cache inside the repository produced the same totals.
  Four `test_check_include_directions.py` fixture tests fail with `WinError 5` creating temporary
  directories. `CcdInputsTests.test_dimer_ccd_points` returns code 1 after the missing-MAGMOM diagnostic,
  and its temporary-directory cleanup also fails with `WinError 5`. Cache creation reports access denied.
  These Python/environment failures are outside 113e's editable scope; no Python fix was attempted.
- No MSBuild, C++ tests, or rebuilt GUI smoke test were run, per the user's build/test ownership.
- `graphify update .` was attempted (AST-only, no API cost), but failed with
  `[graphify watch] Rebuild failed: [WinError 5] Odmowa dostępu`. It warned about inaccessible temporary
  directories from both sessions. The graph was not successfully refreshed.

## Binding adaptations and remaining limits

- `AddCase` accepts an optional activation flag, defaulting to the old behaviour. Folder creation uses
  it to avoid transient active-case changes, loading other cases, or moving their renderer bindings.
- Existing `AttachDataset` deliberately treats dispersion/bulk-mode datasets as shared, so checked
  datasets of those kinds populate case selections instead of exclusive case attachments.
- The one-argument `PhononComparisonColumns` remains the established 12-column compatibility API;
  the UI and full export use the carried-fraction overload with all 22 columns.
- Paths use the existing `DirectoryLabel`: project-relative first, labelled project root second,
  absolute fallback across drives when no root label exists. Full paths are available in tooltips.
- Existing saved figure specifications/files remain snapshots; future exports use filtered plot data.
- Small shared path/export helpers were moved intact between existing topic files to reuse path rules
  and keep the modified IO/plot/batch implementation files below approximately 500 lines. The existing
  oversized project-tree file received only its local menu item.
- The initial permitted pytest command used its normal system temporary directory. The rerun kept
  temporary files inside the repository. Automatic approval review rejected cleanup of
  `.tmp/task-113e-tests` with `blocked by policy`; that directory and pytest cache remnants remain.
- C++ acceptance is unverified until the user builds and runs Debug + Release. No commit was made.
