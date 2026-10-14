# Task 108b: animate the ΔQ direction (GS ↔ ES sweep and ± oscillation around GS)

Plan: `docs/work/project/plans/2026-10-14-vibration-animation.md` (slice 108b; user decision 2026-10-14: BOTH
motions with a switch). Builds on 108a (`VibrationField`, "Animacja drgań" source combo with `kierunek ΔQ`
disabled today).

## Goal

From a CCD analysis dataset the user animates the actual GS -> ES displacement in the structure window:
`przejście GS ↔ ES` (atoms travel between the two relaxed geometries and back, a slider stops at any point Q) or
`drganie wokół GS` (± amplitude along ΔR like a mode). Old CCD datasets without the vector say why the source is
unavailable.

## Design (binding)

1. **Python** (`scripts/python/examples/ccd_analysis.py`, extend - no new script): when the endpoints are known,
   write `ccd_displacement.json` next to `summary.json`:
   ```json
   {"schema": "ds.ccd_displacement.v1", "symbols": [...], "masses_amu": [...],
    "cell_angstrom": [[ax,ay,az],[bx,by,bz],[cx,cy,cz]], "gs_positions_angstrom": [[x,y,z], ...],
    "displacement_angstrom": [[dx,dy,dz], ...], "delta_Q_amu05_A": 1.234,
    "gs_source": "<path relative to the json>", "es_source": "<path relative to the json>"}
   ```
   ΔR exactly as `delta_q_between` computes it (minimum image, centre-of-mass drift removed, GS atom order); refactor
   `delta_q_between` to return the arrays so ΔQ and ΔR come from ONE computation (assert ΔQ =
   sqrt(Σ m |ΔR|²)). Endpoints: `--gs-dir/--es-dir` -> their CONTCARs; `--ccd-dir` -> `gs/<dirname_for(0)>/POSCAR`
   and `es/<dirname_for(1)>/POSCAR` (the generator guarantees they reproduce the relaxed geometries);
   `--phonon-result` only -> no file. `summary.json` gains `"displacement_file": "ccd_displacement.json"` or null.
   Extend the script's self-test / demo if it has one; otherwise add a `--self-test` that checks ΔQ = sqrt(Σ m|ΔR|²)
   on a 2-atom fixture crossing the cell boundary with unequal masses.
2. **IO** (`src/IO/CcdAnalysisIO.{hpp,cpp}`): read the optional file into
   `std::optional<CcdDisplacement> displacement` on `CcdAnalysisResult` (new struct in
   `Domain/Vibrations/CcdAnalysis.hpp`: symbols, masses, cell, gsPositions, displacement, deltaQ); absent -> nullopt
   with no error; malformed -> a load warning, result still loads.
3. **Domain** (`VibrationField.{hpp,cpp}`): `VibrationField CcdDirectionField(const CcdDisplacement &)` (real = ΔR,
   participation = m|ΔR|² / ΔQ², label `kierunek ΔQ · ΔQ 1.23 amu^½ Å`) and a motion kind on the field:
   `enum class VibrationMotion { Oscillate, Sweep };` For `Sweep`, `FieldDisplacements` returns
   `real * extent * (1 - cos φ) / 2` (physical ΔR, amplitude ignored; extent default 1 = exactly GS -> ES, the
   user may go to 1.5 for "beyond ES") - add the extent parameter without breaking 108a callers.
4. **Session/UI**: `Źródło` combo enables `kierunek ΔQ` when the active case's selected CCD dataset has a
   displacement (else disabled with the reason: no CCD dataset / dataset without ΔR - rerun "Policz CCD").
   Controls for this source: `Ruch` combo `przejście GS ↔ ES` / `drganie wokół GS`; for the sweep a `Q` DragFloat
   0..extent·ΔQ (amu^½ Å) that sets the phase while paused and `Zasięg` (1.0..2.0); the amplitude field applies to
   the oscillation. Target window: the bound phonon/structure window when its symbols match the displacement's
   (same rule as `WindowMatches`); otherwise a button `Otwórz geometrię GS` opening the GS endpoint (path from the
   json) as a window bound to the session, the same way `Otwórz superkomórkę` does. Switching the source stops the
   animation first (rest positions restored).

## Files to create or change

- `scripts/python/examples/ccd_analysis.py`
- `src/Domain/Vibrations/CcdAnalysis.hpp`, `src/Domain/Vibrations/VibrationField.{hpp,cpp}`
- `src/IO/CcdAnalysisIO.{hpp,cpp}`
- `src/Presentation/Panels/PhononPanelSession.{hpp,cpp}` (+ a new session file if > 500 lines),
  `PhononPanelSessionWindow.cpp`, `PhononModesPanelView.cpp`
- tests: `tests/Domain/Vibrations/VibrationFieldTests.cpp`, the CCD IO tests (search `CcdAnalysisIO` in `tests/`),
  session tests in `tests/Presentation/Panels/PhononViewsTests.cpp`

## Files that must NOT be touched

- `ccd_inputs.py` (endpoints are already written), dispersion files, structure-building files, `Vendor/`,
  `install/`, `build/`; `docs/` except `docs/work/project/reports/task-108b-ccd-direction.md`

## Acceptance criteria

1. Python self-test passes (`.venv/Scripts/python.exe scripts/python/examples/ccd_analysis.py --self-test` or the
   existing demo), including the boundary-crossing unequal-mass fixture and ΔQ consistency.
2. IO: roundtrip of a synthetic `ccd_displacement.json`; missing file -> nullopt, no error; malformed -> warning.
3. Domain: Sweep at φ = 0 -> 0, φ = π -> ΔR·extent; Oscillate normalised like 108a; participation sums to 1.
4. Session: ΔQ source disabled with the reason without a displacement; with one, sweep then Stop restores the rest
   positions exactly; switching source stops the animation.
5. Existing tests pass. Report: files, tests, how old datasets behave, anything not done.

## Constraints

- Python computes, C++ draws; no new Python dependency.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
