# Task 24: scene object persistence

## Goal
Pinned measurements, free labels and scene arrows survive closing and reopening a project. They are
saved to `scene_objects.yaml` next to `manifest.yaml`, keyed per structure, and loaded back into
every window that shows that structure. Scene edits mark the structure dirty (`*` in the window
title) and Ctrl+S clears it only when both files were written.
Plan: `docs/work/project/plans/2026-09-13-scene-tools-and-group-theory.md`, section 3.

## Files to create or change
Contract (already written, do not change signatures or test expectations):
- `src/IO/SceneObjectsIO.hpp` -> implement `src/IO/SceneObjectsIO.cpp` (yaml-cpp, same style as
  `ProjectManifestIO.cpp`; temp file + rename in `Save`)
- `src/Renderer/Scene/SceneObjectPersistence.hpp` -> implement `.cpp`
- `src/Presentation/ProjectSceneSave.hpp` -> implement `.cpp`
- `src/Domain/ProjectWorkspace.hpp` (`MarkModified`) -> implement in `ProjectWorkspace.cpp`
- `src/Renderer/RendererWindowState.hpp` (new fields `persistKey`, `linkBroken`,
  `frozenAtomPositions`, `frozenAtomElements`)
- Tests: `tests/IO/SceneObjectsIOTests.cpp`, `tests/Renderer/SceneObjectPersistenceTests.cpp`,
  `tests/Presentation/ProjectSceneSaveTests.cpp`

Integration (edit existing files as needed):
1. `src/Renderer/Scene/SceneSystem.cpp` annotation sync: call `EnsureScenePersistKeys`.
2. Every place that duplicates/copies an annotation into a NEW object (e.g.
   `ObjectPropertiesPanel.cpp` ~378/399 copy + `AllocateObjectId`): clear `persistKey` on the copy.
3. Broken links: label anchor resolution (`ResolveLabelAnchor` and anything else reading
   `atomIndices` for a pin) uses `frozenAtomPositions` when `linkBroken`, never indexes atoms then.
   `ObjectPropertiesPanel` "Pinned measurement" section shows a visible "link broken" line.
4. Dirty tracking: every persisted scene mutation - the sites that call
   `PushPinnedMeasurementUndoSnapshot` - and label undo/redo bump the owning structure's revision via
   `StructureRegistry::MarkModified(window.structureId)`. Renderer must not mutate the domain
   registry directly: publish/queue an event through the existing EventBus pattern and let the layer
   that already owns `ProjectWorkspace` apply it. Skip windows with empty `structureId`.
5. Load: when a project opens (EditorLayer project-open path), `SceneObjectsIO::Load` once, keep the
   `SceneObjectsFile` on the EditorLayer, and for each domain-backed window apply the entry whose
   `structureKey == SceneObjectsIO::MakeStructureKey(projectDir, record.sourcePath)`. A window opened
   later: if another window already shows that structure, copy that window's objects
   (Extract -> Apply); otherwise apply from the kept file. Warnings go out as notifications
   (existing StructuredError -> notification path); never abort the project load.
6. Save: `EditorLayer::onProjectSaveRequested` builds a `SceneObjectsFile` - for each structure,
   `MergeWindowSceneObjects` over its windows ordered least -> most recently focused (if no focus
   tracking exists, keep window order with the currently active window last), plus entries from the
   kept file for structures with no open window (so they are not lost) - and calls
   `SaveProjectWithSceneObjects` with the structures written. Failure -> warning notification,
   project stays dirty. Update the kept file on success.

## Files that must NOT be touched
- `src/Presentation/Panels/ViewportGizmo.cpp`, `ViewportLabelGizmo.cpp`,
  `ViewportSceneArrowGizmo.cpp` (task 25 rewrites them) - except a one-line dirty-event call if a
  mutation site lives there
- Anything under `src/Presentation/Panels/GroupTheory*`, `src/Domain/Symmetry/`, `scripts/python/`
- `Vendor/`, `premake5.lua`, `install/`
- Contract headers' signatures and all test expectations

## Acceptance criteria
1. All tests in the three new test files pass; the full suite has no new failures (2 known skips).
2. Add a label, an arrow and a bond pin, Ctrl+S, close, reopen project: all three are back with the
   same text/position/style.
3. After a scene edit the window title shows `*`; Ctrl+S clears it; Ctrl+Alt+U (label undo) brings it back.
4. Delete an atom that a saved pin referenced, reopen without saving the structure change to make
   the reference not bind: the pin still renders at its old place and Properties says link broken.
5. Unknown `kind` in the file: project loads, warning shown, entry gone after the next save.

## Constraints
- Layer boundaries from `CLAUDE.md`/`AGENTS.md`: IO has no Renderer/Presentation includes; Renderer
  does not own domain truth; only the main thread commits state.
- No exceptions escaping render paths (catch yaml-cpp exceptions inside IO).
- `.cpp` files under ~500 lines.
- Reuse `GenerateUuid`/`ToString` (`Core/Utils/Uuid.hpp`) for persist keys (strip dashes, lowercase).
