# Task 107b: datasets next to calculations, auto-scan, comparisons, ZPL formula in the Phonons panel

User decision 2026-10-13 (see 107a for the script side, already merged into this branch):
"fonony bulku nie [powinny być] od tak sobie w panelu fononów, a wynikało to z katalogu który
ustawimy, co jeżeli będziemy chcieli porównać wiele S(ω) czy PL? co jeżeli formuła na ZPL będzie
inna? [...] czy to nie może być w katalogach z obliczeniami? i potem automatycznie wczytane?"

## Goal
1. **Dataset discovery (IO, new `src/IO/AnalysisDatasetIO.{hpp,cpp}`)**
   ```cpp
   enum class AnalysisDatasetKind { Phonons, Dispersion, Ccd };
   struct AnalysisDatasetRef {
       AnalysisDatasetKind kind; Path directory; std::string label; std::string schema;
   };
   // <dir>/summary.json: schema ds.phonon_vibronic.v1 -> Phonons, ds.phonon_dispersion.v1 ->
   // Dispersion, ds.ccd_analysis.v1 -> Ccd; anything else / unreadable -> nullopt. Reads only the
   // schema and "label" (label missing -> directory name).
   [[nodiscard]] std::optional<AnalysisDatasetRef> ReadDatasetRef(const Path &directory);
   // Every child of a directory named "defectstudio" under root (root itself included) whose
   // summary.json is a known dataset; recursive, skip_permission_denied, no symlink following,
   // does not descend below a dataset directory; sorted by path; stops when cancelled() is true.
   [[nodiscard]] std::vector<AnalysisDatasetRef> FindDatasets(const Path &root, const std::function<bool()> &cancelled);
   // Where a new result goes: <deepest common ancestor of inputs>/defectstudio/<kindDirName>;
   // when isWritable(that defectstudio dir's existing ancestor) is false ->
   // <projectDirectory>/defectstudio/<kindDirName>. Empty inputs -> the project fallback.
   [[nodiscard]] Path DefaultDatasetDirectory(const std::vector<Path> &inputs, std::string_view kindDirName,
       const Path &projectDirectory, const std::function<bool(const Path &)> &isWritable);
   [[nodiscard]] bool IsDirectoryWritable(const Path &directory); // create + delete a probe file
   ```
   Kind dir names: `phonons`, `phonons_emb_<mult>` (embedding), `dispersion`, `ccd`.
   Also `PhononAnalysisIO::LoadSpectra(dir, outResult, outError)` - summary + spectral_function.tsv +
   pl.tsv only (no .npy, no modes), for overlays of datasets that are not the active one.
2. **Scan on project load**: when the panel gets its project directory / roots
   (`SetProjectRoots`, project open), run one JobSystem job that calls `FindDatasets` for the project
   directory and every root, then hands the list to the session on the main thread (same pattern as
   the DefectEnergeticsSession run scan). Button "Skanuj ponownie". A finished computation adds its
   dataset without a full rescan.
3. **"Zestawy" section at the top of the panel** (borderless table, accent highlight on the active
   row per UI conventions): columns `porównaj` (checkbox) | Nazwa | Typ (fonony / dyspersja / CCD) |
   Katalog (`PhononPanelSession::DirectoryLabel`, full path in tooltip) | ZPL (formula text or "-").
   Click on a Phonons row = make it the active result (what `resultDirectory` is today: modes table,
   plots, visualisation). Click on a Dispersion row = the dispersion shown in "Dyspersja bulku"
   (replaces the free `dispersionDir` field as the source of what is drawn; the field stays only as
   the optional output override). Ccd rows are listed (used in 105c), not drawn yet.
4. **Comparisons**: S(ℏω) and PL tabs draw, besides the active dataset, every checked Phonons
   dataset (loaded with `LoadSpectra`, cached per directory), each as its own legend entry named by
   the dataset label, with its own legend popup style key `S/<directory label>` / `PL/<directory
   label>`. Stems (S_k, β) stay active-only. The PL of a compared dataset uses the same ZPL choice
   as the active one when available, else its first available curve.
5. **ZPL formula in the inputs**: combo "ZPL" with presets "ΔSCF: E_es − E_gs", "korekcja spinowa:
   2E_es − E_T − E_gs", "eksperymentalne (liczba)", "własny wzór"; selecting a preset fills an
   `InputText` with the formula text (`E_es - E_gs`, `2*E_es - E_T - E_gs`, `1.945`); editing the
   text switches the combo to "własny wzór". Tooltip lists the variables. Collapsible
   "Dodatkowe energie" with folder pickers for E_es@gs and E_gs@es directories. The PL ZPL combo in
   the plots gains "wzór" (`plZpl = "formula"`, curve `plFormula`, value `zplEv.formula`).
   `PhononAnalysisRequest` gains `zplFormula`, `groundAtExcitedDirectory`,
   `excitedAtGroundDirectory`, `label`; `BuildPhononAnalysisArguments` passes `--zpl-formula`,
   `--gs-at-es-dir`, `--es-at-gs-dir`, `--label` (only when set). The old experimental-ZPL field is
   replaced by the formula (a plain number formula = experimental); old settings with `zplExp` load
   as formula text of that number.
6. **Output directory default**: the output field shows (as hint) `DefaultDatasetDirectory(...)` for
   the current inputs; empty field = use it. `ComputationBlocker` overlap rule follows the script:
   output == input or an input inside the output is blocked; output inside an input is allowed.
   Same for "Policz dyspersję" (`<bulk fc dir>/defectstudio/dispersion` by default).
7. **Persistence** (`phonon_panel.yaml`, old files still load): `inputs.zplFormula`,
   `inputs.gsAtEsDir`, `inputs.esAtGsDir`, `datasets: {compared: [paths], dispersion: path}`
   (paths with the existing relative/absolute rule). `resultDir` stays = active phonons dataset.

## Files to create or change
- create `src/IO/AnalysisDatasetIO.{hpp,cpp}`, `tests/IO/AnalysisDatasetIOTests.cpp`
- change `src/IO/PhononAnalysisIO.{hpp,cpp}` (LoadSpectra), `src/IO/PhononPanelSettingsIO.{hpp,cpp}`
- change `src/ScientificRuntime/Python/PhononAnalysisBridge.hpp`, `PhononAnalysisJob.cpp`
- change `src/Presentation/Panels/PhononModesPanel*.{hpp,cpp}`, `PhononPanelSession.{hpp,cpp}`
  (a new `PhononModesPanelDatasets.cpp` is fine if a file would pass ~500 lines)
- tests: `tests/IO/PhononPanelSettingsIOTests.cpp`, `tests/IO/PhononAnalysisIOTests.cpp`,
  `tests/ScientificRuntime/...PhononAnalysisBridge...` (arguments), `tests/Presentation/Panels/PhononPanelSessionTests.cpp`
- run `scripts/Windows/GenerateProjects.bat` after adding files

## Files that must NOT be touched
- `scripts/` (107a is done), `Vendor/`, `install/`, `test-directory/`, `src/Renderer/`, other panels,
  `PlotWidgets.*` (use it)

## Acceptance criteria
1. Reviewer builds Debug+Release, all tests green except the known skips.
2. `AnalysisDatasetIOTests` (temp dirs): finds `a/defectstudio/phonons` and `b/c/defectstudio/dispersion`,
   ignores a `defectstudio/x` without summary.json and one with an unknown schema, does not descend
   into a dataset dir; label fallback = dir name; `DefaultDatasetDirectory` for inputs
   `R/x/relax`, `R/x/excited/s` -> `R/x/defectstudio/phonons`; single input `R/bulk/fc` ->
   `R/bulk/fc/defectstudio/dispersion`; not writable -> project fallback.
3. Settings round-trip with the new keys; an old file with `zplExp: 1.945` loads `zplFormula = "1.945"`.
4. Session test: compared set add/remove/persist, active row selection, dispersion selection.
5. Report what you could not verify (UI is checked by the reviewer in the app).

## Constraints
- Layers: IO does not include Presentation; the scan job touches no UI state; only the main thread
  commits the list to the session. No exceptions on render paths.
- UI conventions: labels left/above (never ImGui's right-hand default), project-relative paths with
  full path in tooltip, disabled buttons say why, legend popups on every series, accent highlight
  for the current row.
- ImGui 1.92 / vendored ImPlot APIs - check the headers; the sandbox cannot build.
- PowerShell built-ins only. Do not edit UTF-8 files with Get-Content/Set-Content. `.cpp` < ~500 lines.
