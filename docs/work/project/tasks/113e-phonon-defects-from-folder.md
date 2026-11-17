# Task 113e: Phonons - add defects from a folder and compare them, like the energetics module

Plan: `docs/work/project/plans/2026-11-17-phonon-server-results-and-comparison.md`. User 2026-11-17: comparing
different defects must work "tak jak to mamy dla modułu termodynamiki" ("bardzo ważne"); the app reads finished
server calculations (113a, done before this task: `FindServerHrResults`, server import through the batch queue,
`phonon_server_import.py`). Energetics reference: `DefectEnergeticsPanelDefects.cpp` (add folder button, list of
added roots with "x", context menu Ukryj/Pokaż / Usuń z analizy, removed list in a tooltip) and the project-tree
item "Dodaj do analizy termodynamicznej" (`ProjectTreePanel.cpp` ~1393, event `EnergeticsFoldersAddRequested`).

## Goal

The user points the `Fonony` window at a folder (local or the mounted cluster drive P:), the app proposes one case
per defect system found below it with its GS / ES / triplet / GS@ES / FC runs pre-picked, the server HR results and
the app datasets that belong to it. The user confirms or changes the picks and creates the cases in one go; server
results are imported automatically (113a machinery). Cases can be hidden (kept, dimmed, out of plots/comparison/
export) and removed; the comparison shows every visible case. Folders are remembered and rescanned on project load,
so new server results show up as "N nowych wyników" on their case.

## Design (binding)

1. **Scanner** `src/IO/PhononDefectFolderScan.{hpp,cpp}`: header written (contract, rules documented there), tests
   `tests/IO/PhononDefectFolderScanTests.cpp` written. Implement; reuse `FindServerHrResults` and `FindDatasets`.
2. **Scan job**: off the main thread, cancellable (the drive is slow), committed on the main thread - same pattern
   as the existing dataset scan job of `PhononPanelSession` (do not write a second job framework).
3. **„Dodaj defekt z folderu…”** button in the cases area of `Fonony` (next to „Nowy przypadek”), plus project-tree
   item „Dodaj do analizy fononów” on folders (multi-select like the energetics item; new event
   `PhononFoldersAddRequested` in `Events/ProjectEvents.hpp`, handled in `EditorLayerAnalysis.cpp` like
   `onEnergeticsFoldersAddRequested`, opening the `Fonony` window). After the scan a popup
   „Nowe przypadki z folderu” lists proposals: checkbox (on), editable name, then one row per role with a combo of
   that role's candidates + „(brak)”; paths shown relative to the project (full path in tooltip); under it the
   server results (supercell, variant) and app datasets found, each with a checkbox (on). Proposals whose GS+ES
   pair already exists in a case are shown unchecked with „już w przypadku <name>”. „Utwórz zaznaczone (N)”:
   per proposal `AddCase(name)`, fill case settings from the picks (bulk FC: the bulk FC dir used by other cases if
   exactly one distinct one exists, else empty), attach the checked app datasets, queue the checked server results
   as server imports into that case (113a), never switch the active case except to the first created one if no case
   existed. Empty scan -> message „Nie znaleziono par GS + ES pod <folder>.”.
4. **Remembered folders**: `PhononPanelSettings::defectRoots` (vector<Path>) and `PhononCase::sourceDirectory`
   (the proposal's system dir; empty for manual cases), `PhononCase::hidden` (bool) - persisted in
   `phonon_panel.yaml` (formatVersion stays 2, keys optional: top-level `defectRoots`, per case `sourceDir`,
   `hidden`; update the schema comment in `PhononPanelSettingsIO.hpp`; round-trip test). Under the button: the list
   of roots with a small „x” (removes the root only, cases stay), like energetics. On project load the roots are
   rescanned (the same job, silently); a case whose `sourceDirectory` has server results not yet imported into it
   (compare `hr_dir` recorded in the case's datasets' summary `server.hr_dir`) shows „N nowych wyników” with a
   button „Wczytaj” (queues them) and a tooltip listing them; a server result whose `hr_factors.dat` mtime is newer
   than the imported dataset's `server.hr_factors_mtime` counts as new too („zaktualizowane na serwerze”).
5. **Hide / show / remove** (case list context menu, multi-select if the list supports it, else single): „Ukryj” /
   „Pokaż” (tooltip as energetics: „Zostaje na liście (przygaszony, na dole), poza wykresami, porównaniem i
   eksportem.”), existing „Usuń z listy” stays. Hidden cases are drawn dimmed at the bottom, skipped by every plot
   adapter, the comparison rows and TSV/figure export, and by „Policz zaznaczone”.
6. **Comparison of defects**: in `Fonony: wykresy` → Porównanie add „Porównaj wszystkie widoczne” (puts the current
   phonon dataset of every visible case into the compared set) and „Wyczyść”. Comparison table rows gain columns
   „źródło” (lokalne / serwer), „komórka” (supercell AxBxC or FC cell), „metoda HR”, „cutoff [Å]” (auto when null),
   E_FC = Σ S_k ℏω_k [meV] - extend `PhononComparisonRow`/`BuildPhononComparisonRows` and the TSV column list in
   the same order (as 108e did). Values missing for old datasets show „—” with a tooltip reason.

## Files to create or change

- new `src/IO/PhononDefectFolderScan.cpp`; `src/IO/PhononPanelSettingsIO*.{hpp,cpp}` (new fields), tests for them
- `src/Events/ProjectEvents.hpp`, `src/Presentation/Panels/ProjectTreePanel.cpp` (one menu item),
  `src/Presentation/EditorLayer.hpp`, `EditorLayerAnalysis.cpp`, `EditorLayerEvents.cpp` (event wiring only)
- `src/Presentation/Panels/PhononPanelSession*.{hpp,cpp}` (new `PhononPanelSessionDefectFolders.cpp` for scan +
  create + new-results logic), `PhononModesPanelCases.cpp` (+ a new `PhononModesPanelDefectFolders.cpp` for the
  popup if the file would pass ~500 lines), `PhononModesPanelComparison.cpp`, `PhononPlotData*.{hpp,cpp}`
- tests: settings round trip, comparison rows (new columns), a session test for "create cases from a proposal"
  if the session has a test access struct for it (see `Tests::PhononBatchTestAccess`)
- `docs/work/project/reports/task-113e-phonon-defects-from-folder.md` (report: files, tests, deviations)

## Files that must NOT be touched

- `src/IO/PhononDefectFolderScan.hpp`, `tests/IO/PhononDefectFolderScanTests.cpp` expectations
- `scripts/`, `Vendor/`, `install/`, `build/`, renderer, structure-building files, energetics files (only read them)
- `src/IO/DataTableIO.*`, `src/Domain/Vibrations/ExperimentalSpectrum.*` (task 113f)
- nothing outside the repo is written; P: is read only

## Acceptance criteria

1. `PhononDefectFolderScanTests` pass; settings round trip with `defectRoots`, `sourceDir`, `hidden`; old
   `phonon_panel.yaml` files load unchanged; comparison rows show the new columns and skip hidden cases.
2. Existing tests pass (I build and run Debug + Release).
3. Report written, including the exact click path for the user: add folder `P:/PL-spectra` -> proposal
   `dimer/CH0/R2SCAN/9x9x1` with relax / try_1_singlet / try_1_tryplet / dispersion and 42+ server results.

## Constraints

- Layers: IO no UI; Presentation adapters (PhononPlotData) without ImGui; only the main thread commits session state.
- `.cpp` files under ~500 lines - split by topic as the existing `PhononPanelSession*.cpp` files are.
- UI rules: paths relative to the project (full path in tooltip), labels left/above, tooltips on every control,
  disabled buttons say why, one-row highlight with the accent colour, DragFloat for continuous values.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
- If a binding detail conflicts with existing code, keep the user-visible behaviour, document the deviation in the
  report and continue - do not stop.
