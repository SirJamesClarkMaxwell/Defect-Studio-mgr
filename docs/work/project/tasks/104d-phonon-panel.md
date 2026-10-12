# Task 104d: Phonons panel - mode table, plots, threshold arrows, animation

## Goal
A "Phonons" panel (Widok -> Analiza -> Fonony) that computes (PhononAnalysisJob, task 104c) or
loads (PhononAnalysisIO, task 104b) a phonon result directory, shows the modes and the vibronic
plots, and draws the selected mode in the renderer window holding the force-constant supercell:
arrows with a participation threshold, and an animation. Every setting persists per project
(`PhononPanelSettingsIO`, 104c). Reference data: `test-directory/sjcm_mgr/PL-spectra/dimer/CH0/R2SCAN/9x9x1`.

## Files to create or change
- `src/Renderer/ModeArrows.cpp` (new) - `BuildModeArrowSegments`
- `src/Renderer/OpenGl/OpenGlRendererBackend.{hpp,cpp}` - draw `RendererWindowState::modeArrows`.
  Extract the GL part of `renderDisplacementArrows` (program setup + instanced shaft/cone draw) into
  one private helper used by BOTH the comparison arrows and the new mode arrows; move
  `DisplacementArrowColor` next to it or reuse the ramp from `ModeArrows.cpp`. No second copy of the
  ~80 lines of uniform setup.
- `src/Presentation/Panels/PhononPanelSession.cpp` (new)
- `src/Presentation/Panels/PhononModesPanel.cpp` (new; split into `PhononModesPanelPlots.cpp` etc.
  if it passes ~500 lines)
- `src/Presentation/EditorLayer*.cpp` - register the panel, load/save `phonon_panel.yaml` on project
  open/close exactly like `loadFnvCorrectionsForProject` (`src/Presentation/EditorLayerAnalysis.cpp`),
  call `PhononModesPanel::StopAnimation()` before project/structure saves and on project change
- `src/Presentation/Panels/ProjectTreePanel*.cpp` - folder RMB submenu "Fonony" with
  "Ustaw jako katalog FC", "... stanu podstawowego", "... stanu wzbudzonego", "... trypletu (korekcja
  spinowa)", "Wczytaj wyniki fononowe" -> an event routed by EditorLayer to
  `PhononModesPanel::SetDirectory` (follow how "Set as Displacement Comparison" is routed)
- `src/Events/*` only if a new event is needed for that RMB route

## Files that must NOT be touched
- the headers `src/Renderer/ModeArrows.hpp`, `src/Presentation/Panels/PhononPanelSession.hpp`,
  `src/Presentation/Panels/PhononModesPanel.hpp` beyond adding private members/helpers to the panel
  (public API is the contract), `src/Renderer/RendererWindowState.hpp` (`modeArrows` already added)
- tests `tests/Renderer/ModeArrowsTests.cpp`, `tests/Presentation/Panels/PhononPanelSessionTests.cpp`
- `src/Domain/Vibrations/*`, `src/IO/Phonon*`, `src/ScientificRuntime/Python/PhononAnalysis*` and their
  tests (tasks 104b/104c - use them, report bugs, do not edit)
- `scripts/`, `Vendor/`, `premake5.lua`

## Panel layout (Polish UI labels, like the other analysis panels)
1. **Wejścia** (collapsing header, open when nothing is loaded): four directory fields (FC, stan
   podstawowy, stan wzbudzony, tryplet - optional) with a "..." folder picker each and the hint that
   the Project Tree RMB "Fonony" sets them too; ZPL eksperymentalne [eV] (checkbox + input),
   sigma [meV], gamma [meV]; katalog wyników (default `<project>/phonons/<name of the FC dir's parent>`,
   editable); buttons **Policz** (PhononAnalysisJob, progress + Anuluj while running) and
   **Wczytaj wyniki...** (folder picker -> PhononAnalysisIO::Load). Errors shown in red under the buttons,
   with the StructuredError userMessage.
2. **Podsumowanie** line: S_total, Debye-Waller, ZPL (obliczone / po korekcji spinowej /
   eksperymentalne), N atoms, a red "mody urojone: n" badge when `!stable` (tooltip lists them),
   and the `approximation` string as a grey tooltip-ed label "przybliżenie harmoniczne, T = 0".
3. Horizontal splitter (draggable, ratio persisted? no - ImGui ini is enough):
   - left: **mode table** (ImGui table, sortable, scrolling, ~16 visible rows): #, ħω [meV], irrep,
     β = N/N_eff, N_eff, S_k, and "N(X%)" = AtomsCarryingFraction for the current fraction (computed
     lazily per visible row). Irrep filter combo above it ("wszystkie" + Irreps()). Click a row = select
     mode; selected row highlighted; ↑/↓ keys move the selection while the table is focused.
   - right: tabs (ImPlot) **β(ħω)** (stems, A1 in colour, other irreps grey - like the user's
     `PL-spectra/scripts/test.py` figure), **S_k i S(ħω)** (S_k stems on the left axis, S(ħω) curve on
     the right axis), **PL** (one curve for the ZPL chosen in a combo: obliczone / po korekcji /
     eksperymentalne; only available ZPLs listed). Clicking near a stem selects the nearest mode; the
     selected mode is marked with a vertical line in the first two tabs. Axis labels use the existing
     TeX-lite helper in `PlotWidgets.hpp` (ħω, S_k, β, E_ZPL).
4. **Wizualizacja**:
   - **Otwórz superkomórkę** -> publish `RendererEvents::Windows::OpenStructureRequested` with
     `<fc dir>/POSCAR`; bind the window whose `structure.sourcePath` equals it (poll each frame until it
     appears). Status text: bound window title or why not (`WindowMatches` error).
   - Pokaż strzałki (checkbox), **próg udziału** (SliderFloat, logarithmic, 1e-4..1, shows the value
     and "strzałek: n z N"), "N atomów niesie X % modu" with X editable (`carriedFraction`), długość
     najdłuższej strzałki [Å], two colour pickers.
   - Animacja: ▶/⏸ (Start/Advance per frame with ImGui delta time), ⏹ (Stop -> rest positions),
     amplituda [Å], okres [s], faza slider (active when paused: SetPhase).
   - Changing the mode, threshold, length, colours or showArrows re-applies arrows immediately;
     changing the mode while animating keeps animating the new mode from the same rest positions
     (Stop then Start).
5. Hiding the panel (closing it) stops the animation and clears `modeArrows` of the bound window.

## Persistence and control (user's standing rules)
- Every value in `PhononPanelSettings` is written through the save handler on change (on release of
  sliders / after edit of inputs, not every frame).
- Nothing happens behind the user's back: the panel never opens a window, starts a computation or
  starts an animation by itself; loading a project restores settings and the result table only.
- The animated positions are never saved: StopAnimation before saves (EditorLayer) and on every exit
  path listed in `PhononPanelSession.hpp`.

## Acceptance criteria
1. `DefectStudioTests.exe --gtest_filter=ModeArrowsTests.*:PhononPanelSessionTests.*` pass; whole
   suite green (only the 2 known skips + any `GTEST_SKIP` for missing data).
2. Manual (the reviewer does it): compute the reference case from the panel, open the supercell,
   select the 200.1 meV A1 mode, see arrows on the C dimer and neighbours, threshold slider hides
   arrows, animation runs and stops with atoms back in place, reopen the project -> same state.
3. `architecture-boundary-review`: Presentation drives the renderer only through window state and
   events, as `DisplacementComparisonPanel` does; no Domain/IO includes of Presentation/Renderer.
4. Each `.cpp` under ~500 lines.

## Constraints
- Session frame formula (window positions may be offset from structure.json):
  `frame_i = rest_i + (AnimatedPositions(result, mode, phase, amplitude)_i - result.positionsAngstrom_i)`.
- ImGui + ImPlot only, no new dependencies. Follow `DisplacementComparisonPanel` for window binding
  and `FnvCorrectionPanel` for job submission/polling and persistence.
- No exceptions in render paths.
- Regenerate projects after adding files (`DS_TOOLSET=msc-v143`, `scripts\Windows\GenerateProjects.bat`).
