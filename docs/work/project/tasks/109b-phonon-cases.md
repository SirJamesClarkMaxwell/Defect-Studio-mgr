# Task 109b: phonon cases (many defects), persistence v2, collision-safe outputs

Plan: `docs/work/project/plans/2026-10-13-phonon-workspace.md`. Builds on 109a (shared `PhononPanelSession`,
three views). User decision 2026-10-13: **a case = one GS->ES transition of one defect in one setup**
(functional, cell); variants (embedding NxN, sigma, ZPL formula, CCD) are datasets inside the case. Flat list.

## Goal

The user analyses and computes many defects in one project: a `Przypadki` list in `Fonony`, switching the active
case swaps every input and the shown results, each case keeps its own inputs, results and view state across
restarts, and two cases never write into the same result directory.

## Design (binding; stop and say why if something does not fit)

1. **Settings shape - minimal churn via a base struct** (`src/IO/PhononPanelSettingsIO.hpp`):
   ```cpp
   // Everything that belongs to one case (inputs, chosen results, per-case view state).
   struct PhononCaseSettings {
       Path forceConstantDirectory, groundStateDirectory, excitedStateDirectory, tripletDirectory;
       std::string zplFormula; Path groundAtExcitedDirectory, excitedAtGroundDirectory, outputDirectory;
       double sigmaMeV = 6.0, gammaMeV = 2.0;
       Path bulkForceConstantDirectory; std::string embeddingMult;
       std::optional<double> dcutoffAngstrom, bcutoffAngstrom;
       Path ccdOutputDirectory; double ccdStep = 0.25; Path ccdDirectory, ccdAnalysisOutputDirectory;
       double ccdTemperatureK = 0.0; bool ccdPhononCurvature = false; Path selectedCcdDirectory;
       Path resultDirectory; int selectedMode = -1; std::string plZpl = "experimental";
       std::string irrepFilter; double minHuangRhys = 0.0;
   };
   struct PhononCase {
       std::string id;              // stable, unique in the file ("case-1", "case-2", ...; never reused in a file)
       std::string name;            // user-visible, editable
       std::vector<Path> datasets;  // attached result directories (phonons / CCD) - the binding membership
       PhononCaseSettings settings;
   };
   // The flat fields of the ACTIVE case stay directly on PhononPanelSettings (inherited), so every existing
   // `settings.forceConstantDirectory` keeps compiling; global fields stay as they are.
   struct PhononPanelSettings : PhononCaseSettings {
       ... // existing global fields (dispersion*, comparedDirectories, seriesStyles, arrows, animation,
           //  thresholds, ccd view toggles) - unchanged
       std::vector<PhononCase> cases;  // cases[i].settings of the active case may be stale; the base is the truth
       std::string activeCaseId;
   };
   ```
   Move the listed fields from `PhononPanelSettings` into the base, same names and defaults. Nothing else moves.
2. **YAML formatVersion 2** (`kFormatVersion = 2`), keys mirror v1:
   ```yaml
   formatVersion: 2
   activeCase: case-2
   cases:
     - {id: case-1, name: "C2 dimer R2SCAN 9x9x1", datasets: [paths], inputs: {fcDir, gsDir, ... v1 case keys},
        resultDir: ..., ccd: path, view: {selectedMode, plZpl, irrepFilter, minHuangRhys}}
   inputs: {dispersionDir, dispersionInPlane, dispersionPoints}
   datasets: {compared: [paths], dispersion: path}
   ... global view/seriesStyles as in v1
   ```
   Serialize writes the active case from the base fields. Parse v2 copies the active case into the base.
   **v1 migration**: a v1 file becomes one case `case-1` named after the result label if `resultDir` has a
   readable dataset label, else `<parent of gs dir>/<gs dir name>`, else `Przypadek 1`; its result and CCD
   directories become attached datasets. A file with no case data -> zero cases (empty project).
   Paths: same relative/absolute rule as today. Files > 500 lines -> split (`PhononPanelSettingsIOCases.cpp`).
3. **Dataset provenance** (`src/IO/AnalysisDatasetIO.{hpp,cpp}`): `AnalysisDatasetRef` gains
   `std::map<std::string, Path> inputs;` read from `summary.json` `inputs_relative` (resolved against the dataset
   directory) falling back to `input_paths`; keys as written by the scripts (`fc_dir`, `gs_dir`, `es_dir`,
   `triplet_dir`, `bulk_fc_dir`, `phonon_result`, ...); null/missing keys are absent. Existing fields unchanged.
4. **Session API** (`PhononPanelSession`, new `PhononPanelSessionCases.cpp`):
   ```cpp
   [[nodiscard]] const std::vector<PhononCase> &Cases() const;    // with the active case synced from the base
   [[nodiscard]] const std::string &ActiveCaseId() const;
   std::string AddCase(std::string name);                          // empty inputs, becomes active, returns id
   std::string DuplicateCase(const std::string &id);               // copy, " (kopia)", outputDirectory and
                                                                   // ccd output overrides cleared, no datasets
   void RemoveCase(const std::string &id);                         // never deletes files; active -> neighbour
   void RenameCase(const std::string &id, std::string name);
   bool ActivateCase(const std::string &id);                       // see 5.
   void AttachDataset(const std::string &caseId, const Path &directory);   // moves it out of any other case
   void DetachDataset(const std::string &caseId, const Path &directory);
   // Datasets whose provenance gs_dir AND es_dir equal the case inputs (SameFilePath), not attached anywhere.
   [[nodiscard]] std::vector<Path> ProposedDatasets(const std::string &caseId) const;
   // One case per distinct (gs_dir, es_dir) among unattached phonons/CCD datasets; inputs filled from provenance
   // (fc, gs, es, triplet, bulk) and the phonons summary (zpl formula text, sigma, gamma); those datasets attached;
   // name = longest common label prefix trimmed of " emb ..."/spaces, else "<gs parent>/<gs name>". Returns ids.
   std::vector<std::string> CreateCasesFromDatasets();
   // Default output dir of a computation for the active case, collision-safe (6.).
   [[nodiscard]] Path CaseOutputDirectory(const std::string &kindDirName) const;
   ```
   Every mutation calls `Changed()` (109a deferred save) and bumps `PlotRevision()` where shown data changes.
5. **Activation**: stop the animation, clear arrows on the old case's window (leave the window open), store the
   base into the old case, copy the new case into the base, load its `resultDirectory` / `selectedCcdDirectory`
   (errors shown, not fatal), forget the bound window id, then bind the new case's window if one is open.
   `SetDirectory` (Project Tree roles) writes into the active case; with zero cases it first `AddCase`s one
   named after the folder.
6. **Collision-safe outputs**: `CaseOutputDirectory(kind)` = today's `DefaultDatasetDirectory(inputs, kind, ...)`
   unless that directory is attached to ANOTHER case, or exists with a summary whose provenance gs_dir/es_dir
   differ from this case's -> then `<kind>_<slug(case name)>` in the same `defectstudio` dir (slug: ASCII
   letters/digits/`-`, others `_`, max 40 chars; still colliding -> append `_2`, `_3`...). `outputDirectory`
   override wins as today. All computation paths (phonons incl. `phonons_emb_<mult>`, CCD analysis, CCD inputs)
   use it; dispersion stays global (bulk).
7. **Window per case**: runtime map case id -> window id in the session (not persisted). Title
   `Superkomórka (fonony) – <case name>`. `CanBindWindow` accepts: source path equality, or title equal to this
   case's title, or the legacy title `Superkomórka (fonony)` - the last two only with `WindowMatches`.
   A window bound to case A is never bound to case B.
8. **UI (`Fonony` view)**: section `Przypadki` above `Zestawy` - table: checkbox `liczyć` (stored in the session
   only, for 109c), `Nazwa`, `Zestawy` (count), `ZPL` / `S` of the case's loaded phonon result if cached, else `-`.
   One accent row = active case (click activates). RMB row: `Zmień nazwę`, `Duplikuj`, `Usuń z listy` (tooltip:
   pliki zostają). Buttons: `Nowy przypadek`, `Przypadki z zestawów` (disabled with reason when nothing is
   unattached). `Zestawy` shows the active case's attached datasets first, then `Proponowane` (with `Przypnij`),
   then a collapsed `Inne` (attached elsewhere / unassigned, with the owning case name); comparison checkboxes work
   in every group (comparisons are global). Dispersion datasets stay global, shown in their own group.

## Files to create or change

- `src/IO/PhononPanelSettingsIO.{hpp,cpp}` (+ split file), `src/IO/AnalysisDatasetIO.{hpp,cpp}`
- `src/Presentation/Panels/PhononPanelSession*.{hpp,cpp}`, `PhononModesPanel*.cpp` (main view sections)
- tests: `tests/IO/PhononPanelSettingsIOTests.cpp`, `tests/IO/AnalysisDatasetIOTests.cpp` (or where they live),
  `tests/Presentation/Panels/PhononPanelSessionTests.cpp`, new `tests/Presentation/Panels/PhononCasesTests.cpp`

## Files that must NOT be touched

- `src/Domain/*`, `src/ScientificRuntime/*`, `scripts/python/*` (provenance is already written by the scripts)
- `src/Renderer/Scene/ModalTransform*`, `src/Presentation/Panels/Viewport*` (another task), energetics/FNV/summary
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-109b-phonon-cases.md`

## Acceptance criteria (GoogleTests)

1. v1 file (today's serializer output) parses into one case with the same inputs, result dir attached; v2
   roundtrip of two cases keeps ids, names, datasets, per-case inputs and view state, active case and globals.
2. `ActivateCase` swaps every `PhononCaseSettings` field, keeps globals (series styles, compared set, arrow colours).
3. `CaseOutputDirectory`: two cases whose inputs share the same common ancestor get different directories;
   a case whose own previous result is there gets the plain directory; slug rules.
4. `CreateCasesFromDatasets` on three datasets (two variants of one transition + one other) -> two cases with the
   right inputs and attachments; running it again creates nothing.
5. `ProposedDatasets` matches by provenance (relative `inputs_relative` resolved), ignores attached ones.
6. Window binding: a window titled for case A is not bound when case B is active; legacy title binds when atoms match.
7. All existing phonon tests still pass unchanged (except the format version constant if a test pins it - say so).

## Constraints

- UI conventions: labels left/above, disabled buttons say why, project-relative paths + full path tooltip, one
  accent row per table, every new control has a tooltip.
- Main thread commits state; no blocking file IO per frame (cache summaries; provenance comes with the scan).
- Regenerate projects after adding files (PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`).
- PowerShell built-ins only; do not commit; if MSBuild cannot write `build/`, say so.
