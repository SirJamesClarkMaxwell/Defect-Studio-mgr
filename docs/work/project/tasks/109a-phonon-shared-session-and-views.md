# Task 109a: Phonons as three linked windows over one shared session

Plan: `docs/work/project/plans/2026-10-13-phonon-workspace.md` (user decisions 2026-10-13).
Pattern to copy: task 101 (`docs/work/project/reports/task-101-linked-panels.md`,
`DefectEnergeticsSession` + `DefectEnergeticsPanel::View`, `EditorLayerAnalysis.cpp`).

## Goal

The Phonons panel is one window that stacks inputs, modes, plots and dispersion. The user wants the plots in
their own window, not tied to one defect, with controls on top, "like thermodynamics". After this task:

- `Fonony` (title unchanged - INI/visibility key): `Zestawy`, inputs, embedding, CCD inputs/compute, dispersion
  inputs/compute. No plots, no mode table.
- `Fonony: mody`: summary line(s), mode table (filters), `Wizualizacja` (window, arrows, animation).
- `Fonony: wykresy`: one tab bar: `β(ℏω)`, `S_k i S(ℏω)`, `PL`, `CCD`, `PL 1D`, `Dyspersja`. Each tab draws what
  it can: phonon tabs need the active result OR at least one compared phonons dataset (today they are gated by
  `Result()`), CCD tabs need a CCD result, `Dyspersja` needs a loaded dispersion; otherwise one disabled line
  saying what to load. The per-tab controls that exist today (ZPL combo on PL, CCD toggles) stay above the plot.
  Plots use the available window height (like the energetics plots after 101), not a fixed 18 lines.
- Behaviour visible to the user is otherwise unchanged (same widgets, same persistence file, same strings).

## Design (follow it; if something does not fit, stop and say why)

1. **Session owns the state; views own only widget state.** Move from `PhononModesPanel` into
   `PhononPanelSession` (new `.cpp` files as needed, each < 500 lines):
   - the four jobs (phonons, CCD inputs, CCD analysis, dispersion), their submit/poll/cancel and error strings
     (`m_Error`, `m_CcdError`, `m_CcdAnalysisError`, `m_DispersionError`, `m_CcdResult`, `m_EmbeddingSummary`);
   - `m_Dispersion`, `m_EmbeddingMult`, `m_DefaultDatasetDirectories`, project directory and roots, dataset scan
     start;
   - window binding and animation driving: `m_WindowId`, `m_WindowsBeforeOpen`, `m_BindRequested`,
     `m_WindowError`, `m_Paused`, `openSupercellWindow` (needs `Ref<EventBus>`), `syncWindow`;
   - `loadResult`, `loadCcdResult`, `loadDispersion`, `SetDirectory`, `SetPersistedState`, `StopAnimation`.
   Keep the existing public session API (tests use it); add to it, do not rename.
2. **Once per frame, from EditorLayer, even when every view is hidden**:
   `void Update(std::vector<RendererWindowState> &windows, double deltaSeconds, bool anyViewVisible);`
   polls jobs and the scan, then binds/syncs the window. When `anyViewVisible` is false: stop the animation,
   clear the bound window's `modeArrows`, drop a pending bind (today's "panel hidden" branch). Closing only the
   plots window must NOT stop an animation shown from `Fonony: mody`.
   `void FlushChanges();` after all views: calls the save handler once if `Changed()` was called this frame.
   Views call `Changed()` where they call `persist()` today.
3. **Plot resets**: the session keeps `std::uint64_t PlotRevision()` bumped whenever a result / CCD / dispersion
   is loaded or the compared set or ZPL choice changes; each view keeps its last seen revision and fits axes when
   it differs (replaces `m_ResetPlots` / `m_ResetDispersionPlot` as cross-view signals). Tab index, zoom and
   `m_ScrollToMode` stay in the view.
4. **Panel** `PhononModesPanel(Ref<PhononPanelSession> session, View view, std::string title, bool visibleByDefault)`
   with `enum class View { Main, Modes, Plots }`. `Clone()` shares the session (101 rule). `GetSubcategory()`
   stays `"Fonony"`. Closing `Fonony` (main) hides the other two (101: "closing a main view closes its siblings");
   the main view gets a small button `Pokaż okna fononów` that shows the other two. Project Tree RMB roles
   (`SetDirectory`) show all three.
5. **EditorLayer** (`EditorLayerAnalysis.cpp`): `registerPhononViews()` creates the session (JobSystem, EventBus)
   and the three panels; `loadPhononsForProject()` talks to the session once (not per panel);
   `stopPhononAnimations()` calls `session->StopAnimation()`; `onPhononDirectoryRequested` -> session.
   `OnImGuiRender` calls `Update(rendererLayer.GetWindows(), io.DeltaTime, anyViewVisible)` before the registry
   renders and `FlushChanges()` after (same places as the energetics session). First-use dock hints via
   `AnalysisDockRegion`: `Fonony` left, `Fonony: mody` bottom, `Fonony: wykresy` right.
6. **Files**: the existing `PhononModesPanel*.cpp` split stays roughly by section; plots move to the Plots view
   (`PhononModesPanelPlots.cpp`, `...CcdPlot.cpp`, `...CcdPlPlot.cpp`, dispersion plot split out of
   `PhononModesPanelDispersion.cpp` if that keeps files < 500 lines).

## Files to create or change

- `src/Presentation/Panels/PhononModesPanel.{hpp,cpp}`, `PhononModesPanel*.cpp` (all of them)
- `src/Presentation/Panels/PhononPanelSession.{hpp,cpp}`, `PhononPanelSession*.cpp`, new
  `PhononPanelSessionJobs.cpp` / `PhononPanelSessionWindow.cpp` (names free)
- `src/Presentation/EditorLayer.{hpp,cpp}`, `src/Presentation/EditorLayerAnalysis.cpp`,
  `src/Presentation/PanelDockRegions.cpp` (hints only)
- `tests/Presentation/Panels/PhononPanelSessionTests.cpp` and a new `tests/Presentation/Panels/PhononViewsTests.cpp`

## Files that must NOT be touched

- `src/IO/*` (no schema change: `phonon_panel.yaml` stays formatVersion 1), `src/Domain/*`,
  `src/ScientificRuntime/*`, `scripts/python/*`
- Energetics / Calculation Summary / FNV panels and sessions
- `Vendor/`, `install/`, `build/`, `docs/` (except a report `docs/work/project/reports/task-109a-phonon-views.md`)

## Acceptance criteria

1. New GoogleTests pass (and every existing phonon test still passes unchanged):
   - two views (and a `Clone()`) share one session: selecting a mode in one is seen by the other;
   - several `Changed()` in one frame -> exactly one save handler call in `FlushChanges()`, none without changes;
   - `Update(windows, dt, false)` with a bound animating window restores rest positions and clears `modeArrows`;
     `Update(windows, dt, true)` keeps animating;
   - `PlotRevision()` changes on result load and on `SetCompared`, not on mode selection;
   - `SetPersistedState` cancels jobs and resets window binding (no stale `WindowId`).
2. `.cpp` files touched or created stay < 500 lines.
3. No ImGui/ImPlot include in `PhononPanelSession*.{hpp,cpp}` beyond what is there today (session stays
   UI-free; delta time comes in as an argument).
4. Report lists files changed, tests added, anything not done.

## Constraints

- Layers per `AGENTS.md`/`CLAUDE.md`; only the main thread commits job results (poll in `Update`).
- UI conventions: labels left/above, disabled buttons say why (`SetItemTooltip` on the disabled item with
  `ImGuiHoveredFlags_AllowWhenDisabled`), project-relative paths with the full path in a tooltip, one accent
  highlight layer per table (`PhononPanelSession::CurrentDataset()` for `Zestawy`, selected mode for modes).
- Do not create a parallel system: reuse `AnalysisDockRegion`, the 101 show/hide handler pattern,
  `PlotHeightHandle` if a plot needs a manual height.
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
  Building: you may not be able to write `build/` (MSB6003); if so, say so - I build and test.
- Use PowerShell built-ins; Git Bash msys tools (head, sed) crash in this sandbox. Do not commit.
