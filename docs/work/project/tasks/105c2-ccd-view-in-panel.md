# Task 105c2: CCD view + 1D PL in the Phonons panel

User 2026-10-13: "CCD dalej nie widać tych parabol, wsp. Q, jak wyglądają wzbudzenia, oraz strzałek
odpowiedzialnych za absorpcję, emisję, i możliwości wizualizacji poziomów i f.f. z nimi skorelowanych
oraz widma PL policzonego bezpośrednio z CCD". Data side done in 105c1
(`scripts/python/examples/ccd_analysis.py`, schema `ds.ccd_analysis.v1`); datasets/auto-scan in 107b
(Ccd rows already listed in "Zestawy"). Read both task files and the current code first.

## Goal
1. **Domain + IO**: `src/Domain/Vibrations/CcdAnalysis.hpp` (`CcdAnalysisResult`: label, ΔQ, ZPL used,
   formula text, `franckCondon` {absorption, emission, relaxationGs, relaxationEs, source},
   harmonic {gs, es: ħω meV, S} (optional), points {branch, t, Q, E (optional), status},
   curves {Q, Vgs, Ves}, levels {gs, es: meV above each bottom}, wavefunctions {Q, gs[k], es[k]},
   pl1d {E, numerov, harmonic, phonon1d - empty when all nan}) and `src/IO/CcdAnalysisIO.{hpp,cpp}`
   `Load(dir, out, error)` reading summary.json, ccd_points.tsv, ccd_curves.tsv, levels.tsv,
   wavefunctions.tsv (header-driven columns `gs_n`/`es_n`), pl_1d.tsv. Missing optional files ->
   empty vectors; wrong schema / malformed -> false with a file-named error. Tests with fixtures,
   plus one test that loads a real output if `test-directory/.../defectstudio/ccd` exists (skip otherwise).
2. **Bridge + job**: `CcdAnalysisRequest` {gsDir, esDir, tripletDir, gsAtEsDir, esAtGsDir, ccdDir,
   phononResultDir, zplFormula, sigmaMeV, temperatureK, label, outputDirectory} ->
   `BuildCcdAnalysisArguments` (flags exactly as ccd_analysis.py `main()` defines them - read it),
   `ParseCcdAnalysisOutput` (last JSON line `{"ok": true, "out_dir", "zpl_eV"}` / `{"ok": false,
   "error"}`), `CcdAnalysisJob` (pattern: PhononAnalysisJob / PhononDispersionJob). Tests for args +
   parse.
3. **Compute** in the panel, section "CCD / PL 1D" (replaces the current CCD-inputs-only block; keep
   the "Generuj punkty CCD" button there): uses the panel's gs/es/triplet/extra-energy dirs and the
   ZPL formula (107b), optional CCD path dir, checkbox "krzywizna z fononów" (= phononResultDir of the
   active Phonons dataset), sigma, T. Output default `DefaultDatasetDirectory(inputs, "ccd", ...)`.
   Disabled button says why. Finished job -> dataset added + made the active CCD dataset.
4. **"CCD" plot tab** (next to β / S_k / PL; active CCD dataset = clicked Ccd row in Zestawy):
   X = Q [amu^½ Å], Y = E [eV] (gs minimum = 0).
   - V_gs, V_es lines; computed points as markers per branch (gs, es, triplet), status != ok drawn
     hollow + tooltip with status.
   - Vibrational levels: horizontal segment per level between the classical turning points of its
     branch (bottom + E_n), first N levels (DragInt "poziomy" 0..8).
   - Wavefunctions χ_n drawn on their level (E_n + scale·χ_n(Q)), checkbox "funkcje falowe",
     radio χ / |χ|², DragFloat "skala χ"; colour follows the branch, legend entry per branch.
   - Arrows (checkbox "strzałki"): absorption ↑ at Q = 0 from V_gs(0) to V_es(0), emission ↓ at ΔQ
     from V_es(ΔQ) to V_gs(ΔQ), labelled with their eV; ZPL as a dashed horizontal from the es
     bottom; E_rel(gs) and E_rel(es) as small vertical brackets with labels; ΔQ as a horizontal
     double arrow under the wells with "ΔQ = … amu^½ Å". Draw with ImPlot items + PlotToPixels on the
     plot draw list (clipped to the plot), text via ImPlot::Annotation / DrawTexText.
   - Info line above the plot: ZPL (formula), ħω_gs/ħω_es, S, absorption, emission, source.
   - Every series has the legend popup (keys `CCD/gs`, `CCD/es`, `CCD/points`, `CCD/levels`,
     `CCD/chi_gs`, `CCD/chi_es`, `CCD/arrows`).
5. **"PL 1D" plot tab**: numerov / harmonic / phonon_1d curves of the active CCD dataset, plus (checkbox
   "porównaj z PL fononów") the active Phonons dataset's PL with the same ZPL choice, plus every
   checked Ccd dataset's numerov curve (label = dataset label). Trimmed to the useful range like PL
   (GridIndexRange). Legend popups.
6. **Persistence**: active CCD dataset, ccd view toggles/sliders, compute inputs (ccdDir, T,
   phonon-curvature checkbox) in `phonon_panel.yaml`; old files load.

## Files to create or change
- create `src/Domain/Vibrations/CcdAnalysis.hpp`, `src/IO/CcdAnalysisIO.{hpp,cpp}`,
  `src/ScientificRuntime/Python/CcdAnalysisJob.{hpp,cpp}` (bridge + job), a new
  `src/Presentation/Panels/PhononModesPanelCcdPlot.cpp` for the two tabs, tests for IO/bridge/settings
- change `PhononModesPanel*.{hpp,cpp}`, `PhononPanelSession.{hpp,cpp}`, `PhononPanelSettingsIO.{hpp,cpp}`
- run `scripts/Windows/GenerateProjects.bat` after adding files

## Files that must NOT be touched
- `scripts/` (done in 105c1), `Vendor/`, `install/`, `test-directory/` (read-only), `src/Renderer/`,
  other panels, `PlotWidgets.*` (use it)

## Acceptance criteria
1. Reviewer builds Debug+Release; tests green except known skips.
2. IO test: fixture with 2 levels/branch and wavefunction columns gs_0, gs_1, es_0, es_1 loads with
   the right shapes; old/missing wavefunctions.tsv -> empty, still true.
3. Arguments test: every flag only when set, paths as Utf8, numbers `{:.10g}`.
4. Report what you could not verify (the plot is checked by the reviewer in the app).

## Constraints
- Layers (Domain has no IO/UI includes; IO no Presentation; job touches no UI state; main thread
  commits). No exceptions on render paths. `.cpp` < ~500 lines.
- UI conventions: labels left/above, project-relative paths, disabled buttons say why, legend popups,
  accent highlight for the current row.
- ImGui 1.92 / vendored ImPlot APIs - check headers; the sandbox cannot build.
- PowerShell built-ins only; do not edit UTF-8 files with Get-Content/Set-Content.
