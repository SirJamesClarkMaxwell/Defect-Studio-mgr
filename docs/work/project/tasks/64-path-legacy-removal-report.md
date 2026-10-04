# Task 64 completion report - 2026-10-04

Runtime legacy arrow removal is implemented. No build, tests, project regeneration or commit was
performed in this Windows sandbox. Caller test rerun and manual app acceptance remain pending.
The existing v1 importer/migration and displacement-arrow rendering are retained.

Caller follow-up: the build succeeded; the full suite reported 1150 passes, one skip and one
failure in the ported mixed atom/path undo test. The failure was the fourth expectation (path
position after redo): its empty path fixture failed `ValidatePath`, so the initial translation
was rejected before either snapshot command ran. The fixture now has two nodes and a line segment,
and checks validation and both translated positions before undo. The original single-entry undo
and both redo checks remain. Production snapshot/group ordering is unchanged. Caller rerun pending.

## Added source files and tests

The only new `.cpp`/`.hpp` is `tests/Renderer/Scene/ScenePathIdentityTests.cpp` (no new headers).
Regenerate the projects to include it and remove deleted source files.
Written before implementation:

- `ScenePathIdentityTests.SelectionSurvivesDeletingADifferentPath`
- `ScenePathIdentityTests.DestroyedIdsStayDeadAndSourceIndicesDropThem`
- `ScenePathIdentityTests.SnapshotRestorePreservesIdsAndRegistryLookups`

## Ported tests and retained coverage

- `SceneObjectModelTests.IdsSurviveAStructureResync`, `RegistryEnumeratesEveryObjectExactlyOnce`,
  `IdsSurviveAnUndoRedoRoundTrip`, `ExplicitIdsAdvanceAllocatorAndDuplicatesGetFreshIds`: paths
  replace runtime arrows; label duplicate-ID coverage remains.
- `SceneObjectsSnapshotCommandTests.UndoRestoresBeforeAndRedoRestoresAfter`: the created-object
  slice now checks paths while retaining labels/pins/orbitals/planes and selection pruning.
- `SceneTransformTests.MixedAtomAndPathConfirmCreatesOneUndoEntryAndRestoresBoth`: grouped atom
  and path undo/redo; normal selection capture still follows the existing scene-before-atoms rule.
- `SceneObjectPersistenceTests.HalfBoundPathKeepsTheFreeEndThroughProjectFileRoundTrip`:
  an authored free endpoint and atom-bound buffered endpoint survive v2 save/load.
- `SceneObjectPersistenceTests.PersistKeysFilledOnce`: paths replace runtime arrow persistence keys.
- `SceneVisibilityTests`: shared selection/hide/show-all/export visibility cases now include paths.
- `ScenePathCutoverTests.FreeLineAndArrowUseSceneRelativeLengthAndFreeNodes` and
  `SavingExtractsOnlyTheCreatedPath`: assert path behavior directly, without a legacy builder.
- `ScenePathShortcutsTests.DeleteRemovesPathsAloneAndAlongsidePlanes`: mixed-kind deletion survives.
- Existing object-property selection, tab-content, edit-action clipboard, region-selection and GL
  path-render cases use the current path types/signatures.
- `KeyBindingIoEventsTests.DefaultSceneShortcutBindingsParseWithoutChordCollisions` additionally
  verifies that the saved legacy reverse binding imports to `renderer.scene_path.reverse`.
- Legacy-only geometry, editor/clipboard, transform-handle, anchor-buffer and deletion cases were
  removed where existing path geometry, bindings, operations and transform tests cover the behavior.
  V1 parsing, fixture, migration and project-save tests remain.

## Decisions beyond the task's explicit file list

- Preserve the atom-buffer control by moving its session default to existing path operations
  (`GetScenePathAtomBuffer`) and retaining the UI control in the existing Add menu.
- Remove the legacy reverse runtime command alias. Map saved `renderer.scene_arrow.reverse`
  command IDs to `renderer.scene_path.reverse` during keymap import, keeping binding IDs, chords
  and user customization. This avoids editing the protected shipped/user keymaps.
- Remove four unused legacy-only arrow renderer settings, their YAML keys and their settings UI.
  Old keys are ignored on read and omitted on the next configuration save.
- Remove the now-redundant operation-specific transform-capture wrapper; callers use the shared
  `CaptureSceneTransformSelection` function directly.
- The task file has no numbered "What to do" step 4; the three identity tests above were added
  before the implementation to honor the requested test ordering.

## Static validation and risks

- `git diff --check` passes for task changes.
- Checked changed C++ file delimiters and project include targets; the GL test helper resolves
  from `tests/Renderer/Gl/GlTestContext.hpp` rather than the production include root.
- Reviewed the render signature and all callers, region-selection signature and callers, scene
  synchronization, snapshots and path-test field types against the actual headers.
- Secondary symbol scan returns zero matches for the deleted runtime geometry/editor/operations,
  transform targets/capture wrapper, registry arrow entries, shader and legacy settings names.
- Ran `graphify update .` (AST only, no LLM/API cost). It failed with Windows access denied
  (`WinError 5`), also recorded by the previous update attempt. The knowledge graph remains stale.
- No new exception paths or layer dependencies were introduced; protected geometry and domain
  trees were not edited. Pre-existing changes in `install/users/**` were left untouched.
- Risks remain compile/test failures that static checks cannot detect and visual regressions in
  selection/transform/export. Caller must regenerate, build and test, then exercise path
  create/select/transform/delete/undo and opening a v1 arrow project.

## Final legacy-name scan

PowerShell `Get-ChildItem src,tests -Recurse -File | Select-String -Pattern
'SceneArrow|sceneArrows|selectedSceneArrows|sceneArrow'` over source and text file types:
**66 matching lines in 13 files**. Every hit belongs to v1 DTO/parser/migration/save handling,
fixtures or tests of those boundaries; no runtime legacy hits remain.

| File | Matching lines | Why retained |
|---|---:|---|
| `src/IO/SceneObjectsIO.cpp` | 5 | V1 DTO/parser |
| `src/IO/SceneObjectsIO.hpp` | 4 | V1 DTO/parser |
| `src/Presentation/ProjectSceneSave.cpp` | 1 | V1 save/migration handling |
| `src/Renderer/Scene/SceneObjectPersistence.cpp` | 1 | V1 save/migration handling |
| `src/Renderer/Scene/ScenePathPersistence.cpp` | 4 | V1 save/migration handling |
| `src/Renderer/Scene/ScenePathPersistence.hpp` | 3 | V1 save/migration handling |
| `tests/IO/SceneObjectsIOTests.cpp` | 14 | V1 parse/migration/save tests or fixtures |
| `tests/IO/SceneObjectsPathIOTests.cpp` | 2 | V1 parse/migration/save tests or fixtures |
| `tests/IO/SceneObjectsV1Fixtures.hpp` | 13 | V1 parse/migration/save tests or fixtures |
| `tests/Presentation/ProjectSceneSaveTests.cpp` | 1 | V1 parse/migration/save tests or fixtures |
| `tests/Presentation/Panels/ScenePathCutoverTests.cpp` | 10 | V1 parse/migration/save tests or fixtures |
| `tests/Renderer/SceneObjectPersistenceTests.cpp` | 2 | V1 parse/migration/save tests or fixtures |
| `tests/Renderer/Scene/ScenePathPersistenceTests.cpp` | 6 | V1 parse/migration/save tests or fixtures |

The old snake-case reverse command ID occurs only in the keymap import conversion and the test
that identifies the unchanged saved binding. No runtime command alias is registered.

## Deleted files

- `src/Presentation/Panels/SceneArrowEditorWidget.cpp`
- `src/Presentation/Panels/SceneArrowEditorWidget.hpp`
- `src/Presentation/Panels/SceneArrowOperations.cpp`
- `src/Presentation/Panels/ViewportSceneArrowInteraction.cpp`
- `src/Renderer/OpenGl/Shaders/arrow_quad.frag`
- `src/Renderer/OpenGl/Shaders/arrow_quad.vert`
- `src/Renderer/Scene/SceneArrowGeometry.cpp`
- `src/Renderer/Scene/SceneArrowGeometry.hpp`
- `tests/Presentation/Panels/SceneArrowOperationsTests.cpp`
- `tests/Renderer/Scene/SceneArrowGeometryTests.cpp`

## Changed files

- `docs/work/project/plans/2026-09-20-path-system-implementation.md`
- `docs/work/project/plans/path-legacy-inventory.md`
- `src/App/Serialization/YamlConfigSerializer.cpp`
- `src/App/Serialization/YamlConfigSerializer.hpp`
- `src/App/Serialization/YamlRendererConfigSection.cpp`
- `src/IO/IOLayer.cpp`
- `src/IO/SceneObjectsIO.hpp`
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp`
- `src/Presentation/Panels/ObjectPropertiesPanelSections.cpp`
- `src/Presentation/Panels/ObjectPropertiesPanelSections.hpp`
- `src/Presentation/Panels/ObjectPropertiesSelection.cpp`
- `src/Presentation/Panels/ObjectPropertiesSelection.hpp`
- `src/Presentation/Panels/ProjectTreePanel.cpp`
- `src/Presentation/Panels/RendererPanel.cpp`
- `src/Presentation/Panels/RendererPanel.hpp`
- `src/Presentation/Panels/RendererPanelContextMenu.cpp`
- `src/Presentation/Panels/RendererPanelOrbitalMenu.cpp`
- `src/Presentation/Panels/RendererTabChrome.cpp`
- `src/Presentation/Panels/SceneObjectEditActions.cpp`
- `src/Presentation/Panels/SceneObjectEditActions.hpp`
- `src/Presentation/Panels/SceneOrbitalEditorWidget.hpp`
- `src/Presentation/Panels/SceneOutlinerPanel.cpp`
- `src/Presentation/Panels/SceneOutlinerPanel.hpp`
- `src/Presentation/Panels/SceneOutlinerRows.cpp`
- `src/Presentation/Panels/ScenePathBindingOperations.cpp`
- `src/Presentation/Panels/ScenePathEditCommands.cpp`
- `src/Presentation/Panels/ScenePathEditCommands.hpp`
- `src/Presentation/Panels/ScenePathOperations.cpp`
- `src/Presentation/Panels/ScenePathOperations.hpp`
- `src/Presentation/Panels/Settings.cpp`
- `src/Presentation/Panels/StructureCreationTabsPanel.cpp`
- `src/Presentation/Panels/ViewportDefectFrame.cpp`
- `src/Presentation/Panels/ViewportGizmo.cpp`
- `src/Presentation/Panels/ViewportGizmo.hpp`
- `src/Presentation/Panels/ViewportInteraction.cpp`
- `src/Presentation/Panels/ViewportLabelInteraction.cpp`
- `src/Presentation/Panels/ViewportModalTransform.cpp`
- `src/Presentation/Panels/ViewportPicking.cpp`
- `src/Presentation/Panels/ViewportPinnedMeasurementInteraction.cpp`
- `src/Presentation/Panels/ViewportRegionSelect.cpp`
- `src/Presentation/Panels/ViewportSceneOrbitalInteraction.cpp`
- `src/Presentation/Panels/ViewportScenePathInteraction.cpp`
- `src/Presentation/Panels/ViewportScenePlaneInteraction.cpp`
- `src/Presentation/Panels/ViewportSelection.hpp`
- `src/Presentation/Panels/ViewportTextEditor.cpp`
- `src/Presentation/Panels/ViewportToolbars.cpp`
- `src/Presentation/Panels/ViewportVacancyAdd.cpp`
- `src/Presentation/Panels/ViewportVacancyAdd.hpp`
- `src/Presentation/Panels/ViewportVacancySelection.cpp`
- `src/Presentation/Panels/ViewportVerticalToolbar.cpp`
- `src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp`
- `src/Renderer/Commands/SceneObjectsSnapshotCommand.hpp`
- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp`
- `src/Renderer/OpenGl/OpenGlRendererBackend.hpp`
- `src/Renderer/OpenGl/Shaders/bonds.frag`
- `src/Renderer/Path/PathDash.hpp`
- `src/Renderer/Path/PathEditSession.hpp`
- `src/Renderer/Path/PathHandleGeometry.hpp`
- `src/Renderer/Path/PathStore.hpp`
- `src/Renderer/Path/PathStyle.hpp`
- `src/Renderer/ProjectSceneWindow.cpp`
- `src/Renderer/RendererConfig.hpp`
- `src/Renderer/RendererLayer.cpp`
- `src/Renderer/RendererLayer.hpp`
- `src/Renderer/RendererMeshData.hpp`
- `src/Renderer/RendererSettings.hpp`
- `src/Renderer/RendererWindowState.hpp`
- `src/Renderer/Scene/SceneFreeLabelAnchors.hpp`
- `src/Renderer/Scene/SceneObject.hpp`
- `src/Renderer/Scene/SceneObjectAppearance.cpp`
- `src/Renderer/Scene/SceneObjectAppearance.hpp`
- `src/Renderer/Scene/SceneObjectPersistence.cpp`
- `src/Renderer/Scene/SceneObjectPersistence.hpp`
- `src/Renderer/Scene/SceneObjectPersistenceSupport.cpp`
- `src/Renderer/Scene/ScenePathObjectOriginPersistence.cpp`
- `src/Renderer/Scene/ScenePlaneGeometry.cpp`
- `src/Renderer/Scene/ScenePlaneGeometry.hpp`
- `src/Renderer/Scene/SceneRegistry.cpp`
- `src/Renderer/Scene/SceneRegistry.hpp`
- `src/Renderer/Scene/SceneSystem.cpp`
- `src/Renderer/Scene/SceneSystem.hpp`
- `src/Renderer/Scene/SceneTransform.cpp`
- `src/Renderer/Scene/SceneTransform.hpp`
- `src/Renderer/Scene/SceneVisibility.cpp`
- `src/Renderer/Scene/SelectionHitTest.hpp`
- `tests/IO/KeyBindingIoEventsTests.cpp`
- `tests/Presentation/Panels/ObjectPropertiesSelectionTests.cpp`
- `tests/Presentation/Panels/RendererTabChromeTests.cpp`
- `tests/Presentation/Panels/SceneObjectEditActionsTests.cpp`
- `tests/Presentation/Panels/SceneObjectEditingTests.cpp`
- `tests/Presentation/Panels/ScenePathBindingOperationsTests.cpp`
- `tests/Presentation/Panels/ScenePathCutoverTests.cpp`
- `tests/Presentation/Panels/ScenePathEditCommandsTests.cpp`
- `tests/Presentation/Panels/ScenePathOperationsTests.cpp`
- `tests/Presentation/Panels/ScenePathShortcutsTests.cpp`
- `tests/Presentation/Panels/ViewportVacancyAddTests.cpp`
- `tests/Renderer/Gl/GlPathRenderTests.cpp`
- `tests/Renderer/Scene/SceneObjectAppearanceTests.cpp`
- `tests/Renderer/Scene/SceneObjectModelTests.cpp`
- `tests/Renderer/Scene/SceneSystemTests.cpp`
- `tests/Renderer/Scene/SceneVisibilityTests.cpp`
- `tests/Renderer/SceneObjectPersistenceTests.cpp`
- `tests/Renderer/SceneObjectsSnapshotCommandTests.cpp`
- `tests/Renderer/SceneTransformTests.cpp`

## Added files

- `tests/Renderer/Scene/ScenePathIdentityTests.cpp`
- `docs/work/project/tasks/64-path-legacy-removal-report.md`
