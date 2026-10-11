# Task 101: linked analysis panels

Implemented five Defect Energetics views and three Calculation Summary views. The conditions view keeps the exact title `Defect Energetics` for existing INI layouts and visibility settings; its content is labelled `Warunki`. All views are in Analysis. Clones share their session while retaining local visibility and plot/UI state.

Added:

- `src/Presentation/Panels/DefectEnergeticsSession.hpp/.cpp`, `DefectEnergeticsSessionFnv.cpp`, `DefectEnergeticsSessionConcentrations.cpp`.
- `src/Presentation/Panels/CalculationSummarySession.hpp/.cpp`.
- `src/Presentation/Panels/DefectEnergeticsPanelContinuations.cpp`.
- `src/Presentation/EditorLayerAnalysis.cpp`.
- `tests/Presentation/Panels/LinkedAnalysisSessionTests.cpp`.

Changed: both panel headers and their rendering files; `EditorLayer.hpp/.cpp`, `EditorLayerDockRegions.cpp`; `PanelDockRegions.hpp/.cpp` and its tests; the Calculation Summary event comment in `ProjectEvents.hpp`.

`EditorLayer::OnImGuiRender()` calls each session's `Update()` once before rendering the registry, including when every view is hidden. It calls `DefectEnergeticsSession::FlushChanges()` once after all views. Workers retain copied inputs; only this main-thread polling accepts their results. Project replacement cancels and detaches energetics jobs, drops deferred saves and clears results/continuations. Input revisions reject old concentration results, mark displayed results stale and disable their export. FNV results go through an explicit EditorLayer callback to the existing FNV panel API.

Formation, CTL, convergence and KS plots use available height. Concentration result tabs contain stacked c/E_F pairs with the existing `PlotHeightHandle`, or a full-height map. Continuation controls moved from the calculation table to the formation plot's tri-state tree; they still affect only thin lines. Run fields use tabs; the chain scrolls independently. `CalculationSummarySession::OpenDirectory()` shows all three registered views.

Docking uses first-use hints into existing left/bottom/right/central regions, falling back to the central stack. It does not create the exact 25/50/25 layout or split existing nodes; saved INI placement wins. YAML, cache and TSV schemas and the numerical routines are unchanged. No new dependencies. All affected panel/session `.cpp` files and the new EditorLayer analysis file are below 500 lines; the pre-existing large `EditorLayer.cpp` was shortened rather than refactored wholesale.

Added GoogleTests cover clone sharing, single deferred save, duplicate concentration dispatch, main-thread result acceptance, stale/project results and docking hints. Checked source declarations/member references, delimiter balance, file lengths and `git diff --check`. Build, GoogleTests and GUI were **not run**, as requested; premake was not run. New source files must be included when regenerating the user's projects before building. MSVC compilation, actual docking, narrow windows and interactions need validation on that build.

`graphify-out/graph.json` was absent. `graphify update .` was attempted but failed with `[WinError 5] Access denied`, so no graph refresh was possible. No commits were made; `Vendor/`, `install/` and `build/` were untouched.
