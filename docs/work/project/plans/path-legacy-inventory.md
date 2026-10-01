# Legacy SceneArrow inventory (PathSystem S14 deliverable)

Generated 2026-10-02 on `task/48-path-s14` with
`grep -rlE "SceneArrow|sceneArrows|selectedSceneArrows|sceneArrow" src tests` - **81 files**
(57 `src`, 24 `tests`). Per v2 plan section 5 every reference is classified so S15 (cutover) can
change routing only and S16 (removal) knows what to delete. Re-run the grep before S15/S16; the
footprint is a moving target.

Classes:

- **S** serialization - v1 DTO / importer / migration. **Stays** after S16 (loading old projects).
- **R** runtime routing - creation entry points, menus, shortcuts, interaction chain. S15 re-points
  or removes reachability; S16 deletes what is left.
- **C** compile-time replacement - types, geometry, rendering, state. Deleted in S16.
- **P** behavioural parity - a feature the legacy arrow has that ScenePath must already provide
  behind the dev route before S15 is allowed. Listed separately below with its status.
- **T** test - delete with the code it tests, or port the arrow case to ScenePath first.

## Parity gate for S15

| Legacy behaviour | Where | ScenePath status |
|---|---|---|
| Endpoint anchored to an atom, with buffer | `SceneSystem::RefreshAnchoredSceneArrows`, `ViewportInteraction.cpp:31` | resolver exists; live context wired in task 48b; binding UI in 48c |
| Reverse (Alt+R) | `SceneArrowOperations.cpp` `ReverseSelectedSceneArrowsCommand` | `renderer.path_edit.reverse` (Edit Mode) and Properties button; **no Object Mode multi-path reverse command yet** |
| Copy / duplicate / paste | `SceneArrowOperations.cpp`, `ViewportLabelInteraction.cpp` | done (`ScenePathOperations`) |
| "Adjust last operation" popup after add | `RendererPanel::renderSceneArrowQuickEditPanel`, `sceneArrowQuickEditActive` | **missing** - decide at S15: port or drop |
| Draw arrow/line through selected atoms | `RendererPanelOrbitalMenu.cpp:99-108` (add menu) | **check** - path creation presets exist (S11); "through selected atoms" variant not confirmed |
| Outliner row, eye/camera columns | `SceneOutlinerPanel.cpp`, `SceneOutlinerRows.cpp` | done |
| Region (box/circle) select | `ViewportRegionSelect.cpp` | done (`ViewportRegionPathSelect.cpp`) |
| Mixed G/R/S with atoms | `SceneTransform.{hpp,cpp}` arrow target | done (`SceneTransformPaths.cpp`) |
| Hide / show-all, visibility undo | `SceneVisibility.cpp` | done |
| Handle markers in the structure-creation viewport | `StructureCreationTabsPanel.cpp:316` | **check** - path overlay is drawn only by `RendererPanel` |
| Export preview / PNG | `OpenGlRendererBackend` `renderSceneArrows` | path pass exists; bound nodes in export covered by 48b |

## Source files

| File | Refs | Class | Note |
|---|---:|---|---|
| `src/IO/SceneObjectsIO.hpp` | 4 | S | `PersistedSceneArrow` DTO - keep |
| `src/IO/SceneObjectsIO.cpp` | 5 | S | `ParseArrow` - keep for v1 load; stop *writing* arrows at S15 |
| `src/Renderer/Scene/ScenePathPersistence.hpp` | 2 | S | v1 -> v2 migration - keep |
| `src/Renderer/Scene/ScenePathPersistence.cpp` | 1 | S | `MigrateArrowToPath` - keep |
| `src/Renderer/Scene/SceneObjectPersistence.hpp` | 1 | S/C | comment; arrow apply path goes at S16 |
| `src/Renderer/Scene/SceneObjectPersistence.cpp` | 8 | S/C | window <-> file for `sceneArrows`; S15 load route -> migration only, S16 delete runtime arrow write |
| `src/Presentation/Panels/RendererPanel.cpp` | 32 | R | arrow interaction, quick-edit panel, add menu wiring |
| `src/Presentation/Panels/RendererPanel.hpp` | 3 | R | quick-edit panel declaration |
| `src/Presentation/Panels/RendererPanelOrbitalMenu.cpp` | 18 | R | Add-menu arrow kinds (creation entry point) |
| `src/Presentation/Panels/ViewportToolbars.cpp` | 1 | R | include only (toolbar add button) |
| `src/Presentation/Panels/ViewportVerticalToolbar.cpp` | 1 | R | include only (toolbar add button) |
| `src/Presentation/Panels/ViewportInteraction.cpp` | 2 | R | `RefreshAnchoredSceneArrows` + `HandleSceneArrowInteraction` in the click chain |
| `src/Presentation/Panels/ViewportLabelInteraction.cpp` | 29 | R | arrow clipboard keys, Delete, `renderer.scene_arrow.reverse` registration |
| `src/Presentation/Panels/ObjectPropertiesPanel.cpp` | 2 | R | arrow Properties section route |
| `src/Presentation/Panels/ObjectPropertiesSelection.cpp` | 1 | R | `sections.arrows` |
| `src/Presentation/Panels/ObjectPropertiesPanelSections.cpp` | 13 | R | arrow Properties section |
| `src/Presentation/Panels/SceneObjectEditActions.cpp` | 8 | R | `SceneObjectEditKind::Arrow` |
| `src/Presentation/Panels/SceneOutlinerPanel.cpp` | 7 | R | arrow rows, visibility columns |
| `src/Presentation/Panels/SceneOutlinerPanel.hpp` | 2 | R | `drawSceneArrowRow` |
| `src/Presentation/Panels/SceneOutlinerRows.cpp` | 10 | R | arrow rows |
| `src/Presentation/Panels/StructureCreationTabsPanel.cpp` | 1 | R | `DrawSceneArrowHandleMarkers` in the creation viewport |
| `src/Presentation/Panels/RendererTabChrome.cpp` | 1 | R | "window has content" predicate - add/keep paths there |
| `src/Presentation/Panels/ViewportPicking.cpp` | 2 | C | selection clear |
| `src/Presentation/Panels/ViewportSceneOrbitalInteraction.cpp` | 2 | C | selection clear + comment |
| `src/Presentation/Panels/ViewportScenePathInteraction.cpp` | 1 | C | selection clear |
| `src/Presentation/Panels/ViewportScenePlaneInteraction.cpp` | 1 | C | selection clear |
| `src/Presentation/Panels/ViewportRegionSelect.cpp` | 14 | C | arrow region hit tests |
| `src/Presentation/Panels/ViewportSelection.hpp` | 3 | C | arrow hit-test / interaction declarations |
| `src/Presentation/Panels/ViewportSceneArrowInteraction.cpp` | 36 | C | whole file |
| `src/Presentation/Panels/ViewportGizmo.cpp` | 40 | C | arrow handle geometry / drag |
| `src/Presentation/Panels/ViewportGizmo.hpp` | 6 | C | `SceneArrowHandleGeometry` |
| `src/Presentation/Panels/SceneArrowEditorWidget.cpp` | 34 | C | whole file |
| `src/Presentation/Panels/SceneArrowEditorWidget.hpp` | 33 | C | whole file |
| `src/Presentation/Panels/SceneArrowOperations.cpp` | 76 | C | whole file |
| `src/Presentation/Panels/SceneOrbitalEditorWidget.hpp` | 3 | C | comments referring to the arrow contract - reword |
| `src/Renderer/RendererWindowState.hpp` | 27 | C | `SceneArrow`, `sceneArrows`, `selectedSceneArrows`, quick-edit and drag state |
| `src/Renderer/RendererLayer.cpp` | 10 | C | arrow style clipboard, snapshot plumbing |
| `src/Renderer/RendererLayer.hpp` | 2 | C | comments / clipboard |
| `src/Renderer/RendererSettings.hpp` | 3 | C | default Arrow3D/Line proportions |
| `src/Renderer/RendererMeshData.hpp` | 1 | C | comment only - **keep `cone`**: `renderDisplacementArrows` draws `m_ConeMesh` too |
| `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` | 38 | C | `renderSceneArrows`; **keep `m_ConeMesh`** (displacement); fix the "only caller" comments at :529 and :1428 |
| `src/Renderer/OpenGl/OpenGlRendererBackend.hpp` | 19 | C | arrow render inputs |
| `src/Renderer/OpenGl/Shaders/arrow_quad.vert` | 2 | C | Arrow2D quad shader - delete with the pass |
| `src/Renderer/OpenGl/Shaders/bonds.frag` | 1 | C | comment only |
| `src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp` | 5 | C | `sceneArrows` slice of the snapshot |
| `src/Renderer/Scene/SceneArrowGeometry.cpp` | 20 | C | whole file (tip table + dash logic already lifted to `Renderer/Path`, v2 C14) |
| `src/Renderer/Scene/SceneArrowGeometry.hpp` | 11 | C | whole file |
| `src/Renderer/Scene/SceneObjectAppearance.cpp` | 3 | C | arrow colours / shaft segments |
| `src/Renderer/Scene/SceneObjectAppearance.hpp` | 4 | C | same |
| `src/Renderer/Scene/SceneObject.hpp` | 2 | C | `SceneObjectKind::SceneArrow` |
| `src/Renderer/Scene/SceneRegistry.cpp` | 1 | C | kind display name |
| `src/Renderer/Scene/SceneRegistry.hpp` | 1 | C | comment |
| `src/Renderer/Scene/SceneSystem.cpp` | 17 | C/P | `AppendSceneArrow`, `RefreshAnchoredSceneArrows`, registry sync |
| `src/Renderer/Scene/SceneSystem.hpp` | 8 | C | same |
| `src/Renderer/Scene/SceneTransform.cpp` | 27 | C | arrow transform target |
| `src/Renderer/Scene/SceneTransform.hpp` | 3 | C | `SceneArrowTransformTarget` |
| `src/Renderer/Scene/SceneVisibility.cpp` | 4 | C | arrow visibility |
| `src/Renderer/Scene/SelectionHitTest.hpp` | 2 | C | comment only |
| `src/Renderer/Scene/ScenePlaneGeometry.cpp` | 1 | C | comment (`MakeDefaultSceneArrow` sizing) - reword |
| `src/Renderer/Scene/ScenePlaneGeometry.hpp` | 1 | C | comment - reword |
| `src/Renderer/Path/PathDash.hpp` | 1 | C | comment about the legacy wrapper - drop at S16 |
| `src/Renderer/Path/PathStyle.hpp` | 1 | S | legacy `ArrowTip` mapping - needed by the migration, keep |
| `src/Renderer/Path/PathEditSession.hpp` | 1 | C | comment only |

## Test files

| File | Refs | Class | Note |
|---|---:|---|---|
| `tests/IO/SceneObjectsV1Fixtures.hpp` | 13 | S | v1 fixtures - keep |
| `tests/IO/SceneObjectsIOTests.cpp` | 14 | S | v1 parse - keep |
| `tests/IO/SceneObjectsPathIOTests.cpp` | 2 | S | keep |
| `tests/Renderer/Scene/ScenePathPersistenceTests.cpp` | 6 | S | migration - keep |
| `tests/Renderer/SceneObjectPersistenceTests.cpp` | 29 | T | port arrow round-trips to paths, then drop |
| `tests/Presentation/Panels/SceneArrowOperationsTests.cpp` | 127 | T | delete at S16 (reverse/clipboard covered by path tests) |
| `tests/Renderer/Scene/SceneArrowGeometryTests.cpp` | 14 | T | delete at S16 |
| `tests/Renderer/SceneTransformTests.cpp` | 53 | T | drop arrow cases |
| `tests/Renderer/Scene/SceneObjectModelTests.cpp` | 48 | T | drop arrow cases |
| `tests/Renderer/Scene/SceneSystemTests.cpp` | 37 | T | drop arrow cases (anchor refresh -> binding tests) |
| `tests/Renderer/Scene/SceneVisibilityTests.cpp` | 20 | T | drop arrow cases |
| `tests/Renderer/Scene/SceneObjectAppearanceTests.cpp` | 10 | T | drop arrow cases |
| `tests/Presentation/Panels/SceneObjectEditingTests.cpp` | 11 | T | drop arrow cases |
| `tests/Renderer/SceneObjectsSnapshotCommandTests.cpp` | 9 | T | drop arrow cases |
| `tests/Presentation/Panels/ScenePathShortcutsTests.cpp` | 8 | T | drop arrow cases |
| `tests/Presentation/Panels/RendererTabChromeTests.cpp` | 4 | T | drop arrow cases |
| `tests/Presentation/Panels/SceneObjectEditActionsTests.cpp` | 3 | T | drop arrow cases |
| `tests/Presentation/Panels/ObjectPropertiesSelectionTests.cpp` | 1 | T | drop arrow case |

(The grep also matches a few test files counted above only once; re-run it for the exact list.)
