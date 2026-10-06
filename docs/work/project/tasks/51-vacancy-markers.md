# Task 51: vacancy markers

First task of `docs/work/project/plans/vacancy-and-defect-orbitals.md` - read its "Why" and the 51
section first.

## Goal

A vacancy (`CrystalStructure::vacancies`, recorded today by every atom delete and then forgotten)
is drawn as a camera-facing disc with a dashed ring in the vacancy style's colour, can be added at
the selection centroid or the 3D cursor and removed from the Scene Outliner (undoably), survives
saving and reopening the project, and is restored when an atom delete is undone.

## The contract, already written

Read every comment in these; they are the spec.

- `src/Renderer/RendererTypes.hpp` - `RendererVacancyData`, `RendererStructureData::vacancies`.
- `src/Renderer/AtomStyleTable.hpp` - `VacancyRenderStyle::dashCount` / `ringWidth`,
  `AtomStyleTable::SetVacancyStyle`.
- `src/Domain/Defects/DefectModel.hpp` - `MakeVacancySite`.
- `src/Renderer/Scene/VacancyMarkerGeometry.hpp` - **new**, `BuildVacancyMarkerMesh` and its exact
  geometry rules.
- `src/Renderer/Commands/RendererVacancyCommands.hpp` - **new**, `CreateSetVacanciesCommand`,
  `SetVacanciesPayload`, the command id and payload key.
- `src/IO/SceneObjectsIO.hpp` - `PersistedVacancy`, `PersistedStructureSceneObjects::vacancies`, the
  schema comment.
- `src/Renderer/RendererWindowState.hpp` - `showVacancies`.
- Tests: `tests/Renderer/Scene/VacancyMarkerGeometryTests.cpp`,
  `tests/Domain/Defects/VacancySiteTests.cpp`, `tests/Renderer/VacancyRendererDataTests.cpp`,
  `tests/IO/SceneObjectsVacanciesIOTests.cpp`.

## What already exists - reuse it

- `ApplyVacancy` (DefectModel.cpp) - already records a `VacancySite` on every atom delete.
- `VacancyRenderStyle` + `AtomStyleIO` (`vacancy:` section of the atom-style YAML) - add
  `dash_count` / `ring_width` there, optional on read, always written.
- `OpenGlScenePlaneRenderer.cpp` - the model for the backend pass: build a soup of
  `IsosurfaceVertex`, upload it to a `OpenGlMeshHandles` in `OpenGlViewportResources`, draw with
  `renderIsosurfaceGpuOverlay`. Fill: colour = vacancy colour, alpha = opacity (1 for Solid). Ring:
  a second draw, colour = vacancy colour * 0.6, alpha 1. One upload per soup for ALL vacancies is
  fine (they share a style). The ring width floor: like `ScenePlaneBorderWidth`, at least 1.5 px
  via `WorldUnitsPerPixelAt`. Camera right/up come from the camera's view matrix rows.
- `RendererAtomEditCommands.cpp` - `ResolveAtomEditTarget`, `RebuildAndSync`. If `RebuildAndSync` is
  file-local, expose it (declaration in `RendererAtomEditCommands.hpp`) - do not copy it.
- `RendererCommandRegistration.cpp` - register `renderer.vacancy.set` exactly like
  `renderer.gizmo.commit_transform` (payload through `CommandContext::TryGet`, no keybinding).
- `EditorLayer.cpp` `applySceneObjectsToWindow` / `saveProjectWindowState` - where per-structure
  scene objects are loaded and saved; vacancies go next to them.
- `SceneOutlinerPanel.cpp` `drawPlanesGroup` - the model for a new group.

## Files to create or change

- `src/Domain/Defects/DefectModel.cpp` - `MakeVacancySite`.
- `src/Renderer/AtomStyleTable.cpp` - `SetVacancyStyle` (mutate the shared style in place).
- `src/IO/AtomStyleIO.cpp` - `dash_count`, `ring_width`.
- `src/Renderer/StructureRendererDataBuilder.cpp` - fill `vacancies` from `structure.vacancies` and
  `GetVacancyStyle()`, label = `GetLabel()`.
- `src/Renderer/Scene/VacancyMarkerGeometry.cpp` - **new**.
- `src/Renderer/OpenGl/OpenGlVacancyRenderer.cpp` - **new**, `OpenGlRendererBackend::renderVacancyMarkers`
  (declare it in `OpenGlRendererBackend.hpp`, add a mesh-handles slot to the viewport resources).
  Drawn after atoms and bonds, in the same pass as the scene planes, only when `showVacancies`.
  Pass `showVacancies` to `RenderWindow` as a new trailing defaulted parameter (`= true`), and from
  `RendererLayer` (also copy it into the export preview state next to `showCellBox`).
- `src/Renderer/Commands/RendererVacancyCommands.cpp` - **new**; registration in
  `RendererCommandRegistration.cpp`.
- `src/Renderer/Commands/RendererAtomEditCommands.cpp` - `DeleteSelectedAtomsCommand` remembers
  `structure.vacancies` with the atoms and bonds, and `Undo` restores it.
- `src/IO/SceneObjectsIO.cpp` (and `SceneObjectsYaml.cpp` if that is where entries are parsed) -
  `vacancies:` per structure entry.
- `src/Presentation/EditorLayer.cpp` - save: each structure's domain `vacancies` into its
  `PersistedStructureSceneObjects` (a structure with vacancies and no scene objects still gets an
  entry). Load: when a structure's entry has vacancies and the domain structure has none yet, copy
  them into the domain structure once and rebuild every open window showing that structure.
  The mapping is a plain field copy; keep it in EditorLayer (or its project-scene .cpp) - IO stays
  free of Domain types.
- `src/Presentation/Panels/SceneOutlinerPanel.cpp` (+ `SceneOutlinerRows.cpp` if that is where
  groups live) - "Wakanse (N)" group, shown when N > 0: group eye toggles
  `windowState.showVacancies`; one row per vacancy = label + position (2 decimals) + a small "X"
  that executes `renderer.vacancy.set` with the list minus that entry.
- Add-vacancy action, placed next to the existing "delete selected atoms" entry in the viewport's
  edit/context menu (find where `renderer.atom_edit.delete...` is offered in the UI): "Dodaj wakans
  (centroid zaznaczenia)" when >= 1 atom is selected - plain mean of the selected atoms' Cartesian
  positions, sourceSpecies empty; otherwise "Dodaj wakans (kursor 3D)" at `cursor3DPosition`.
  Executes `renderer.vacancy.set` with the current list + `MakeVacancySite(...)`.
  ponytail: plain mean, no minimum-image unwrap - a vacancy whose neighbours straddle the cell
  boundary lands in the wrong place; unwrap when someone hits it.
- `src/Presentation/Panels/ElementCatalogPanel.cpp` - a "Wakans" row/section: colour, radius,
  opacity, mode (Ghost/Wireframe/Solid), dashes, ring width. Commit through `SetVacancyStyle`, then
  rebuild the open windows the same way an element style edit reaches them (find how
  `SetElementStyleCommand` / the catalog propagates an element edit and do the same; the existing
  save of `GetVacancyStyle()` stays).

Regenerate projects after adding files: `DS_TOOLSET=msc-v143` + `scripts\Windows\GenerateProjects.bat`.

## Files that must NOT be touched

- The contract headers' declarations and comments listed above, and the four test files.
- Everything under `src/Renderer/Path/**`, `*Bevel*`, `PathStrokeMesher*`, `PathSolidMesher*`,
  `PathDecorationMesher*`.
- `Vendor/**` (private, gitignored - never read into, copy from, or commit).
- Shaders: no new shader; the isosurface overlay pipeline is reused.

## Acceptance criteria

1. Release build of `DefectStudio` and `DefectStudioTests` succeeds.
2. `DefectStudioTests --gtest_filter=VacancyMarkerGeometryTests.*:VacancySiteTests.*:VacancyRendererDataTests.*:SceneObjectsVacanciesIOTests.*:SceneObjectsIOTests.*:AtomStyleIoEventsTests.*:StructureToRendererTests.*`
   passes.
3. Full suite: no new failures (known: 5 `PathStrokeMesherTests` bevel failures, 1 skip).

## Constraints

- Layer rules (`CLAUDE.md`): Domain knows no renderer; IO knows no Domain-to-view mapping;
  Presentation mutates domain state only through commands (`renderer.vacancy.set`), never by
  assigning `structure.vacancies` from a panel. The one exception is the project load path, which
  already builds domain state directly.
- No exceptions on render paths; errors as `StructuredError`.
- `.cpp` files under ~500 lines; `OpenGlRendererBackend.cpp` is already far over - put the new
  pass in its own file.
