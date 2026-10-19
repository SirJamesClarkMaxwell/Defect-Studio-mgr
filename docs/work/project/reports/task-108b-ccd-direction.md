# Task 108b: CCD displacement direction and per-case animation settings

Implemented design items 1–5 on the existing VibrationField, shared session and YAML v2 case flow.
The animation section is drawn outside the phonon-result guard; summary and mode table remain
guarded. CCD-only cases can select the direction source without loading phonon modes.

## Behavior

- Python computes minimum-image ΔR and removes mass-weighted centre-of-mass drift once. The same
  arrays supply ΔQ and `ccd_displacement.json`. Relaxed-point inputs use their CONTCARs; CCD paths
  use `gs/<dirname_for(0)>/POSCAR` and `es/<dirname_for(1)>/POSCAR` when available. Phonon-only
  inputs write no displacement file. The summary records the filename or null. Endpoint paths
  use the final published directory, rather than the temporary staging directory. Different
  Windows drives retain absolute paths, following the existing provenance writer's convention.
- CcdAnalysisIO reads the optional geometry, resolves endpoint paths, and validates shapes,
  ordered symbols, finite values, positive masses, nonsingular cells and the mass-weighted norm.
  Missing displacement files leave nullopt without an error. Malformed displacement files add
  `CcdAnalysisResult::loadWarnings` while retaining the analysis and spectra.
- CcdDirectionField carries physical ΔR, GS equilibrium coordinates and mass-weighted
  participation. Sweep motion is `ΔR * extent * (1 - cos(phase)) / 2`, independent of amplitude.
  Oscillation uses the existing cycle normalisation. The original three-argument
  FieldDisplacements API is retained; the four-argument overload accepts extent.
- The source combo enables the CCD direction when displacement data exists. Missing datasets
  and legacy datasets without ΔR have explicit reasons. The controls provide motion selection,
  extent 1–2, paused Q editing and oscillation amplitude. Q editing from a stopped state captures
  the rest positions and previews a paused frame. CCD motion is based on GS positions, and Stop
  restores the exact captured window positions. Switching source or motion stops first.
- Matching ordered symbols permit use of the bound structure window. The GS opening button
  reuses the existing open-event and session-window binding flow with the endpoint path.
- Source, θ, motion, parked Q and extent are case fields serialized beside selectedMode. Invalid
  values warn and retain defaults; old v1/v2 settings retain defaults. Project loading and case
  switching restore these settings without starting animation. A saved CCD source without ΔR
  remains selected with its disabled reason. Explicit mode changes reset θ; existing direct
  SetResult replacement/reset expectations are retained. Sweeps update Q in memory; pause,
  Q editing and Stop use the existing deferred save flow, without saving every animation frame.

## Files changed

- `scripts/python/examples/ccd_analysis.py`
- `src/Domain/Vibrations/CcdAnalysis.hpp`
- `src/Domain/Vibrations/VibrationField.hpp`
- `src/Domain/Vibrations/VibrationField.cpp`
- `src/IO/CcdAnalysisIO.hpp`
- `src/IO/CcdAnalysisIO.cpp`
- `src/IO/PhononPanelSettingsIO.hpp`
- `src/IO/PhononPanelSettingsIO.cpp`
- `src/IO/PhononPanelSettingsIOCases.cpp`
- `src/Presentation/Panels/PhononPanelSession.hpp`
- `src/Presentation/Panels/PhononPanelSession.cpp`
- `src/Presentation/Panels/PhononPanelSessionWindow.cpp`
- `src/Presentation/Panels/PhononModesPanelView.cpp`
- `src/Presentation/Panels/PhononModesPanel.cpp` (only moving the animation draw call outside the guard)
- `tests/Domain/Vibrations/VibrationFieldTests.cpp`
- `tests/IO/CcdAnalysisIOTests.cpp`
- `tests/IO/PhononPanelSettingsIOTests.cpp`
- `tests/Presentation/Panels/PhononViewsTests.cpp`
- This report.

No new source file is needed: the session implementation remains below 500 lines. Existing shared
headers were read before local patches; existing formatting and test expectations were retained.
The user's task-file edits and unrelated workspace files were left untouched.

## Tests added or extended

- Python `--self-test`: two unequal masses, periodic boundary crossing, known ΔR, zero mass-weighted
  drift, ordered symbols and ΔQ consistency.
- New domain test: sweep endpoints, return to GS, extent, amplitude independence, participation,
  sweep arrows, normalised oscillation and invalid ΔQ.
- New CCD IO test: synthetic displacement JSON loading, all geometry fields and resolved source,
  missing-file behavior, malformed JSON/schema/shapes/masses/norm/source/cell warnings, retained PL.
- Two new session tests: CCD-only availability, sweep/Stop, normalised GS oscillation, symbol
  mismatch, source switching, two-case YAML roundtrip, restored θ and unavailable saved CCD source
  without autoplay. Extended the actual phonon-file loading test to check saved θ and stopped state.
- Extended settings tests: distinct values of all five keys in two cases; old-file defaults;
  unknown enums, nonfinite numbers and invalid numeric ranges default with warnings.

All existing assertions remain present; no old test expectation was changed.

## Verification and anything not done

- `.venv/Scripts/python.exe scripts/python/examples/ccd_analysis.py --self-test` passed.
- `.venv/Scripts/python.exe -m unittest scripts.python.tests.test_ccd_analysis` passed: 13 tests.
- An additional synthetic CCD endpoint CLI run checked the written vector norm against both
  displacement and summary ΔQ, and verified both relative paths against the final output directory.
- Tree-sitter C++ grammar checks found no new errors. The existing session-header parser finding
  at the constructor default argument is unchanged from HEAD. This is not a compilation check.
- `git diff --check` passed. Production .cpp files stay below 500 lines; the settings test file is
  approximately 500 lines (516), retaining its existing layout and adding only task assertions.
- Projects were regenerated using the requested DS_TOOLSET / GenerateProjects.bat command.
  Premake completed. The wrapper's submodule step emitted the known sandbox Git sh.exe
  CreateFileMapping / Win32 error 5; project generation itself succeeded.
- MSBuild, C++ GoogleTests and interactive GUI checks were not run, as requested. Compilation,
  executable test validation and visual checks remain for the user's build. No commit was made.
- Graphify was queried for navigation and returned insufficient current context. Graphify update
  was not run because the explicit allowed-file scope excludes writes to graphify-out.
- Context7 was unavailable. The new UI calls follow existing widget patterns and were checked
  against the official Dear ImGui header; no Vendor sources were read for API questions.

Old CCD datasets continue to load. Their direction source stays unavailable until a CCD analysis
with endpoint geometry writes the new optional displacement file. No requested implementation
item was deferred; C++ execution and GUI validation remain unverified.

## Review fixes (Claude, 2026-10-20)

- `ccd_analysis.py`: with `--ccd-dir` the well separation stays the ΔQ between the sampled minima (as before 108b);
  the geometric ΔQ is recorded only in `ccd_displacement.json`, so earlier CCD results do not change.
- Session: animation frames are the window's rest positions + field, also for the ΔQ source (the json GS positions
  are not used as the base; wrapping of the opened geometry may differ). Test expectations follow.
- Verified: Debug tests + app build, 1800 tests (5 expected skips) before the fixes, phonon/CCD subset 176/176
  after; Python CCD tests 16 passed, `--self-test` ok. GUI not checked.
