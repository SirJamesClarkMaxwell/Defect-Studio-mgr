# Task 69 implementation report

Implemented one Add catalogue shared by RMB, Shift+A, and all three drawing/orbital toolbar popups. It includes atom insertion, vacancies/bonds/labels, defect axes, selected and free line/arrow/plane, task 68's curved arrow, all existing path presets and development gallery, all orbital presets, free text, text tool, and measurement/label actions. Disabled rows explain their requirements; measurement and text-tool rows show their existing shortcuts. No separate main-menu Add entry was found.

Atom insertion reuses the coordinate editor, including the Shift+A fractional-position flag. Toolbar calls now receive the existing command registry so vacancy and defect-axis edits work through their established commands. Text opens the existing inline editor. Measurement events explicitly target the menu's window.

Single- and multi-orbital panels share the aim controls. Every eligible orbital aims from its own resolved centre; nearest vacancy is resolved per centre. Each batch pushes one scene-object undo snapshot. LCAO, two-centre and axisless orbitals are skipped for aiming, as are coincident/non-finite targets. Mixed selections still expose the controls for eligible orbitals.

Axis mapping is available in the defect context submenu and the orbital/plane N panels. It covers all nine own-axis/defect-axis combinations. The secondary own axis is x (y if x is primary), placed along defect x (y if defect x is primary); the remaining axis completes a right-handed frame. Rotations use the existing individual-origin transform path. Two-centre/LCAO orbitals have no independent orientation and are excluded. Explicit alignment detaches fitted planes in the same undo snapshot: otherwise ResolveAnchoredScenePlanes immediately overwrites the selected frame. The existing full-frame alignment menu now uses the same helper.

The outliner previously rendered every open window; its rows were already based on each window's structure registry. It now uses ResolveActiveRendererWindowId, the same visible-tab/last-focused resolver as the toolbar. The root row names the displayed scene, keyboard edits and Escape affect only that scene, and scene changes reset rename/range-selection state. Render and its local visibility helpers moved into SceneOutlinerRender.cpp to keep the implementation files below roughly 500 lines.

## Tests

- Added three SceneOrbitalAimTests cases: distinct anchored centres with input immutability, unsupported/coincident/stale selection, nearest vacancy per centre and duplicate IDs.
- Added five SceneAxisAlignmentTests cases: all nine mappings/right-handed frames/secondary axes, invalid inputs, anchored plane y along defect z with centre/size preservation, eligible orbital selection/anchors, and path rotation about its own origin.
- Caller command: `DefectStudioTests.exe --gtest_filter=SceneOrbitalAimTests.*:SceneAxisAlignmentTests.*`.

## Validation

Project generation passed with `DS_TOOLSET=msc-v143` and `Vendor/Binaries/Premake/Windows/premake5.exe vs2022`; generated application and test projects include all new source files. Premake printed existing deprecation warnings. Direct Premake invocation avoids the wrapper's submodule initialization step while other agents are working.

Task-owned tracked diffs passed `git diff --check`; new files passed whitespace and embedded-NUL checks. Builds and C++ test execution were deliberately not run, per task 69's caller-build constraint. Runtime screenshot comparison remains for the caller's build.

Graph refresh: `graphify update .` and the source-scoped retry failed because the Windows sandbox denies the multiprocessing named pipe. Sequential `graphify.extract.extract(..., parallel=False)` and `build_merge`/`to_json` refreshed the task-owned files in `graphify-out/graph.json` (573 AST nodes / 1262 edges extracted). Existing communities were retained; new unclustered nodes are marked as such. The broad GRAPH_REPORT.md/community analysis was not regenerated. AST cache entries under graphify-out/cache were updated.

## Files changed by this task

Concurrent task 68/caller edits in the working tree are not included below. In ViewportVacancyAdd.cpp only DrawDefectAddItems was moved; bond logic was left to the caller. ViewportDefectFrame.cpp edits are menu drawing/wiring and their includes.

- `src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp`
- `src/Presentation/Panels/ObjectPropertiesPanelSections.cpp`
- `src/Presentation/Panels/RendererPanel.cpp`
- `src/Presentation/Panels/RendererPanelContextMenu.cpp`
- `src/Presentation/Panels/RendererPanelOrbitalMenu.cpp`
- `src/Presentation/Panels/RendererPanelToolbar.cpp`
- `src/Presentation/Panels/RendererTabChrome.cpp`
- `src/Presentation/Panels/RendererTabChrome.hpp`
- `src/Presentation/Panels/SceneOrientationControls.cpp`
- `src/Presentation/Panels/SceneOrientationControls.hpp`
- `src/Presentation/Panels/SceneOutlinerPanel.cpp`
- `src/Presentation/Panels/SceneOutlinerPanel.hpp`
- `src/Presentation/Panels/SceneOutlinerRender.cpp`
- `src/Presentation/Panels/StructureCreationTabsPanel.cpp`
- `src/Presentation/Panels/ViewportAddMenu.cpp`
- `src/Presentation/Panels/ViewportAddMenu.hpp`
- `src/Presentation/Panels/ViewportDefectFrame.cpp`
- `src/Presentation/Panels/ViewportToolbars.hpp`
- `src/Presentation/Panels/ViewportVacancyAdd.cpp`
- `src/Presentation/Panels/ViewportVerticalToolbar.cpp`
- `src/Renderer/RendererWindowState.hpp`
- `src/Renderer/Scene/SceneAxisAlignment.cpp`
- `src/Renderer/Scene/SceneAxisAlignment.hpp`
- `src/Renderer/Scene/SceneOrbitalAim.cpp`
- `src/Renderer/Scene/SceneOrbitalAim.hpp`
- `tests/Renderer/Scene/SceneAxisAlignmentTests.cpp`
- `tests/Renderer/Scene/SceneOrbitalAimTests.cpp`
- `docs/work/project/tasks/69-ui-round-2-report.md` (this report)

Ignored generated outputs updated: `build/generated/vs2022/DefectStudio.vcxproj`, `DefectStudio.vcxproj.filters`, `DefectStudioTests.vcxproj`, and `DefectStudioTests.vcxproj.filters`.

Ignored graph outputs updated: `graphify-out/graph.json`, `graphify-out/manifest.json`, and AST cache entries under `graphify-out/cache/`. Existing community labels and broad report were retained.

Additional generated debugging artifacts: `.codex/task69-owned-files.json` (the task-owned file inventory), `src/Renderer/Scene/graphify-out/cache/stat-index.json`, and empty AST-cache directories beneath that source-scoped retry directory. Automatic approval review rejected shell cleanup with "blocked by policy"; the subsequent file-patch cleanup was rejected as "writing outside of the project; rejected by user approval settings". These optional artifacts remain; no build sources depend on them.
