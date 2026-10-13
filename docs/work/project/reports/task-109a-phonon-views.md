# Task 109a: phonon shared session and linked views

Implemented three registered views over one `PhononPanelSession`:

- `Fonony`: datasets, phonon/embedding inputs, CCD generation/analysis and dispersion computation.
- `Fonony: mody`: summary, filtered modes table and window/arrows/animation controls.
- `Fonony: wykresy`: one tab bar for β, S_k/S, PL, CCD, PL 1D and dispersion, with controls above available-height plots.

The existing `Fonony` title and `Fonony` subcategory are preserved. First-use docking uses the existing left/bottom/right regions. Closing the main view hides its siblings and shared-session clones; its show button and directory requests show the linked views. Closing plots leaves animation running while another view remains visible.

The session owns the four computation jobs, errors, embedding state, dispersion, project roots/output-directory cache, scan dispatch and renderer window binding. EditorLayer updates it once before panel rendering, including with hidden views, and flushes deferred saves once afterwards. Hidden updates restore animated positions, clear arrows and drop pending window binding. Project replacement cancels/detaches jobs and the scan, clears window binding/results/errors and discards deferred edits. Clones share the session while retaining widget state.

Every existing public session method and its default construction remain available. Plot revisions replace panel reset flags. Result/CCD/dispersion loads, comparisons and ZPL choices revise plots; mode selection does not. Compared-only S/PL work without an active result. β can lazily upgrade the existing spectra cache with mode data; failed upgrades leave cached spectra available. No second cache system, event system, persistence schema or dependency was added. `phonon_panel.yaml` remains formatVersion 1.

## Files changed

Existing presentation files:

- `src/Presentation/EditorLayer.hpp`
- `src/Presentation/EditorLayer.cpp`
- `src/Presentation/EditorLayerAnalysis.cpp`
- `src/Presentation/PanelDockRegions.cpp`
- `src/Presentation/Panels/PhononModesPanel.hpp`
- `src/Presentation/Panels/PhononModesPanel.cpp`
- `src/Presentation/Panels/PhononModesPanelInputs.cpp`
- `src/Presentation/Panels/PhononModesPanelDatasets.cpp`
- `src/Presentation/Panels/PhononModesPanelView.cpp`
- `src/Presentation/Panels/PhononModesPanelPlots.cpp`
- `src/Presentation/Panels/PhononModesPanelCcd.cpp`
- `src/Presentation/Panels/PhononModesPanelCcdPlot.cpp`
- `src/Presentation/Panels/PhononModesPanelCcdPlPlot.cpp`
- `src/Presentation/Panels/PhononModesPanelDispersion.cpp`
- `src/Presentation/Panels/PhononPanelSession.hpp`
- `src/Presentation/Panels/PhononPanelSession.cpp`
- `src/Presentation/Panels/PhononPanelSessionCcd.cpp`
- `src/Presentation/Panels/PhononPanelSessionDatasets.cpp`

New files:

- `src/Presentation/Panels/PhononPanelSessionState.cpp`
- `src/Presentation/Panels/PhononPanelSessionJobs.cpp`
- `src/Presentation/Panels/PhononPanelSessionCcdJobs.cpp`
- `src/Presentation/Panels/PhononPanelSessionWindow.cpp`
- `src/Presentation/Panels/PhononPanelSessionLifecycle.cpp`
- `src/Presentation/Panels/PhononPanelSessionEmbedding.cpp` (moved from the removed `PhononModesPanelEmbedding.cpp`)
- `src/Presentation/Panels/PhononModesPanelDispersionPlot.cpp`
- `src/Presentation/EditorLayerPanels.cpp`
- `src/Presentation/EditorLayerPalette.cpp`
- `src/Presentation/EditorLayerWindowState.cpp`
- `src/Presentation/EditorLayerProject.cpp`
- `src/Presentation/EditorLayerEvents.cpp`
- `src/Presentation/EditorLayerSaving.cpp`
- `tests/Presentation/Panels/PhononViewsTests.cpp`
- This report.

The extra EditorLayer files are mechanical moves of the existing sections, necessary to bring the roughly 2,000-line original below the requested 500-line limit. Existing moved method bodies were checked against HEAD; only frame update/detach, registration and project-root wiring changed.

## Tests added

Nine GoogleTests in `PhononViewsTests.cpp` cover:

- Views and clones sharing mode selection while retaining independent visibility.
- Multiple changes producing one deferred save; project replacement dropping pending edits.
- Hidden views restoring positions/clearing arrows; closing plots preserving animation in modes.
- Plot revision changes for results, CCD, comparisons and ZPL, and stability on mode selection.
- Project replacement cancelling each of the four queued job types and the dataset scan, restoring positions and clearing WindowId.
- Compared spectra remaining available without active phonons, including after a failed mode-cache upgrade.
- Lazy mode-cache upgrade, cached failures, invalidation and actual result-directory loading.
- Completed dataset scans committing once through a hidden main-thread Update.
- Existing docking hints for all three titles.

Existing phonon session and dataset-session tests were left byte-for-byte unchanged relative to HEAD.

## Verification and anything not done

- Public session API preservation, file lengths, session UI independence and moved editor method bodies checked. All touched/new `.cpp` files are below 500 lines; the largest is `EditorLayerWindowState.cpp` at 460 lines. Tree-sitter parsed the C++ translation units successfully; this is a grammar check, not compilation. `git diff --check` passed.
- Regenerated projects with `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`. Premake completed and both generated projects include the new sources. The wrapper's submodule step printed the known Git `sh.exe` CreateFileMapping sandbox error; premake still succeeded.
- **Build and GoogleTests remain unverified.** The MSBuild path in CLAUDE.md was absent. Located the installed toolchain with vswhere and attempted Debug app/tests using `D:/Aplications/VisualStudio/VisualStudioIDE/MSBuild/Current/Bin/MSBuild.exe`. It stopped with **MSB3491 access denied** writing GLFW/GLAD dependency `.lastbuildstate` files under Vendor. Stopped build attempts; no Release build or test executables were run. GUI/docking interactions also need validation on the user's build.
- Ran `graphify update .`; its AST-only rebuild failed with **WinError 5: Access denied**, so the knowledge graph could not be refreshed.
- Creating `task/109a-phonon-shared-views` failed because `.git/refs/heads/...lock` was not writable. Changes remain on `task/109-phonon-workspace`. No commit was made.
- No task edits to IO, Domain, ScientificRuntime, Python bridges/scripts, other analysis panels/sessions, Vendor or install. Existing and concurrent unrelated workspace changes were preserved. Generated build project files were produced only through the requested regeneration command.
