# Task 49: the Project Scene - a structure-free window that saves with its project

Implements `docs/superpowers/specs/2026-09-30-project-scene-persistence-design.md`. **Read the spec
first**; this file only maps it onto the code.

## Goal

A renderer window with no structure behind it is transient today: `EditorLayer::saveProjectWindowState`
gathers only windows with a `structureId`, so paths authored in an empty window are lost on exit.
After this task every open project owns exactly one "Project Scene" window, its scene objects are
saved into `scene_objects.yaml` under `projectObjects`, and they come back when the project is
reopened - including when the app is launched with `--project=<directory>`. This is what lets a
bevel diagnostic gallery live in a saved project that anyone can reopen.

## The contract, already written

- `src/Renderer/ProjectSceneWindow.hpp` - **new**; Find / Reset / Gather. **Read every comment.**
- `src/IO/SceneObjectsIO.hpp` - `SceneObjectsFile::projectObjects` + schema comment.
- `src/Renderer/RendererWindowState.hpp` - `isProjectScene`, `sceneObjectsDirty`.
- `src/App/ApplicationState.hpp` - `ApplicationSpecification::startupProjectDirectory`.
- Tests:
  - `tests/Renderer/ProjectSceneWindowTests.cpp` (spec criteria 2, 3-renderer half, 4, 6)
  - `tests/IO/SceneObjectsProjectObjectsIOTests.cpp` (criterion 1)
  - `tests/App/ApplicationArgumentsTests.cpp` (criterion 5, parsing half)

## What already exists - reuse it

- `OpenEmptyRendererWindow` (`src/Renderer/OpenCrystalStructureAsWindow.cpp`) - the bootstrap for a
  structure-free window (`BuildRendererStartupWindows` with an empty definition). The project-scene
  window is built the same way, then given `windowId = kProjectSceneWindowId` and
  `isProjectScene = true`. Do not write a second window builder.
- `ExtractPersistedSceneObjects` / `ApplyPersistedSceneObjects` /
  `SceneSystem::SyncLabelEntities` (`src/Renderer/Scene/SceneObjectPersistence.hpp`) - the one
  window <-> persisted conversion. Gather and Reset are thin wrappers over them.
- `SceneObjectsIO::Parse` / `Serialize` (`src/IO/SceneObjectsIO.cpp`) - extend them for
  `projectObjects` using the same per-object entry reader/writer `structures[].objects` uses. Absent
  -> empty; present but not a sequence -> `Parse` returns false with an `outError` naming
  `projectObjects` (same rule as `structures`); empty -> not written.
- `QueueSceneObjectsModified` (`src/Renderer/RendererLayer.cpp` ~line 53) - every scene-object undo
  push and replay already goes through it; it currently returns early for a nil `structureId`. Set
  `sceneObjectsDirty` there for a project-scene window (it is the one place both push and undo/redo
  reach).
- `ApplicationDetail::ParseApplicationArguments` (`src/App/ApplicationBootstrap.cpp`) - add
  `--project=` next to `--log-file=`; also add it to the `--help` usage line.

## Files to create or change

- `src/Renderer/ProjectSceneWindow.cpp` - **new**.
- `src/IO/SceneObjectsIO.cpp` - parse/serialize `projectObjects`.
- `src/Renderer/RendererLayer.cpp` - `QueueSceneObjectsModified` sets the dirty flag (needs a
  non-const window or a const_cast-free route - your call, keep it local).
- `src/App/ApplicationBootstrap.cpp` - parse `--project=`.
- Wherever the composition root hands startup data to `EditorLayer` (`src/App/...`) - pass
  `startupProjectDirectory` through. Keep `App` a composition root: it forwards the value, it does
  not open the project itself.
- `src/Presentation/EditorLayer.cpp` - the runtime flow from the spec:
  - `loadInitialProjectState`: a startup project directory, when set, is tried first; if its
    manifest fails to load, report it (log + notification, same shape as the existing
    "most recent project failed to load" path) and continue with the recents rule.
  - after any project becomes active (`loadInitialProjectState`, `openProject`, `createNewProject`
    - they all call `loadSceneObjectsForProject`): `ResetProjectSceneWindow` with the loaded
    `projectObjects` (empty for a new project), surfacing its warnings like the other load
    warnings. With no project active there is no project-scene window to create.
  - `saveProjectWindowState`: `sceneObjects.projectObjects = GatherProjectSceneObjects(...)` next to
    the structure grouping; on success clear the project-scene window's `sceneObjectsDirty`.
    `m_KeptSceneObjects` must keep `projectObjects` too (it is the merge base for the next save).
  - `applySceneObjectsToWindow` must keep skipping the project-scene window (it is nil-structure;
    verify the existing early return still holds and that nothing else applies structure objects
    to it).
  - **Keep new code out of `EditorLayer.cpp` where it can live elsewhere** - that file is ~1950
    lines. If the project-scene glue is more than a handful of lines, put it in
    `src/Presentation/EditorLayerProjectScene.cpp` as `EditorLayer` member definitions, the way
    `EditorLayerMenus.cpp` / `EditorLayerDockRegions.cpp` already split the class.
- `src/Presentation/Panels/RendererPanel.cpp` (~line 190) - append `"*"` to the title of a
  project-scene window whose `sceneObjectsDirty` is set, next to the existing structure-dirty rule.
- `docs/superpowers/specs/2026-09-30-project-scene-persistence-design.md` - append a short
  "Status" line: implemented in task 49.

Then regenerate projects (new files): `DS_TOOLSET=msc-v143` + `scripts\Windows\GenerateProjects.bat`.

## Files that must NOT be touched

- The contract header `src/Renderer/ProjectSceneWindow.hpp`, the three contract fields above, and
  the three test files.
- `src/Renderer/Scene/SceneObjectPersistence.*`, `src/Renderer/Scene/ScenePathPersistence.*` - the
  conversion is reused, not changed.
- `SceneObjectsIO::Save` / `WriteBackupOnce` / the version guard.
- Everything bevel-related (`PathSolidMesher*`, `PathDecorationMesher*`, `PathStrokeMesher*`,
  `*Bevel*`) and everything under `src/Renderer/Path/`.
- `src/Presentation/Panels/ScenePathEditCommands.*`, `ScenePathOperations.*`.
- `project_windows.txt` save/restore logic - the project-scene window is recreated from the project,
  not from that file. If restore would create a duplicate structure-free window for it, skip
  structure-free records there and say so in the report.

## Acceptance criteria

1. Release build of DefectStudio and DefectStudioTests succeeds.
2. `DefectStudioTests --gtest_filter=ProjectSceneWindowTests.*:SceneObjectsProjectObjectsIOTests.*:ApplicationArgumentsTests.*:SceneObjectsPathIOTests.*:SceneObjectsIOTests.*`
   passes.
3. Full suite: no new failures. Known before this task: 5 `PathStrokeMesherTests` bevel failures
   (out of scope) and the ConPty skip.
4. A `scene_objects.yaml` written by a build without this task loads unchanged (covered by the IO
   test); a project that never used the Project Scene saves without a `projectObjects` key.

## Constraints

- Layer boundaries (`AGENTS.md`): `IO` knows nothing of windows; `Renderer` must not include
  `Presentation`; `App` forwards, does not orchestrate.
- Main thread only; no exceptions; errors as `StructuredError`.
- New `.cpp` files under ~500 lines.
