# Task 20: shared scene object model

Workstream: [2. scene object model](../plans/visualization-and-group-theory/02-scene-object-model.md)
Branch: `task/20-scene-object-model`

## What the workstream file got wrong

The plan says `src/Renderer/Scene/SceneComponents.hpp` "IS the visual-object model - extend it, do
not start a parallel hierarchy." That is half true, and the wrong half is load-bearing:

- The `entt` registry is a **side mirror**, not the model. `OpenGlRendererBackend` never touches
  `entt::registry` or `SceneRegistry` at all - it renders from the flat arrays on
  `RendererStructureData` / `RendererWindowState`. `SceneSystem` rebuilds the registry *from* those
  arrays and pushes selection/visibility back into them.
- Only atoms, bonds and pinned measurement labels are mirrored. **Scene arrows and free labels have
  no entity and no component**; they exist only as `std::vector<SceneArrow>` /
  `std::vector<FreeLabel>` on `RendererWindowState`.
- Identity is a raw vector index. `selectedSceneArrows`, `selectedFreeLabels` and
  `selectedPinnedMeasurements` are `std::vector<std::size_t>` of positions in those vectors, and
  `SyncSceneWithStructure` destroys and recreates every entity from scratch on any structure change.
- There is no provenance, no stale flag and no style preset anywhere. `ObjectPropertiesPanel.cpp`
  (1100 lines) has no type dispatch - it is five hardcoded `if` blocks, one per object kind, each
  looping its own vector. `SceneOutlinerPanel.cpp` rebuilds its tree by re-scanning those same
  vectors by index every frame.

So the model does not exist yet, in either place. This task builds it on top of the existing mirror
pattern rather than beside it.

## Goal

Every object in the scene - atom, bond, pinned measurement, free label, scene arrow - gets a stable
identity that survives a structure resync, and one place that enumerates all of them. The Scene
Outliner stops re-deriving its tree from five separate vectors and iterates the registry instead;
selection stops being a vector index.

The point is the cost of the *next* object kind. Today, adding one - a plane in workstream 3, a path
in 4, an orbital in 5 - costs a new vector on `RendererWindowState`, a new hardcoded block in the
properties panel, a new block in the outliner, a new field in the undo snapshot, and a new backend
path. After this task the first four collapse into registering one kind.

## Files to create or change

- `src/Renderer/Scene/SceneObject.hpp` (new) - `SceneObjectId` (opaque, stable, monotonic per
  window), `SceneObjectKind` enum, `SceneObjectComponent` (id, kind, source index, display name,
  provenance, stale flag).
- `src/Renderer/Scene/SceneRegistry.{hpp,cpp}` - id to entity lookup alongside the existing three
  index to entity vectors; id allocation.
- `src/Renderer/Scene/SceneSystem.{hpp,cpp}` - mirror scene arrows and free labels as entities too,
  and preserve ids across `SyncSceneWithStructure` instead of destroying and renumbering.
- `src/Renderer/RendererWindowState.hpp` - a `SceneObjectId` field on `SceneArrow`, `FreeLabel` and
  `PinnedMeasurement`; the three selection vectors become vectors of `SceneObjectId`.
- `src/Presentation/Panels/SceneOutlinerPanel.cpp` - enumerate
  `registry.view<SceneObjectComponent>()` instead of scanning five vectors.
- Call sites that read the selection vectors, updated mechanically:
  `ViewportSceneArrowGizmo.cpp`, `ViewportSceneArrowInteraction.cpp`, `ViewportLabelInteraction.cpp`,
  `ViewportRegionSelect.cpp`, `RendererLayer.cpp`, `ObjectPropertiesPanel.cpp`.
- `tests/Renderer/SceneObjectModelTests.cpp` (new).

## Files that must NOT be touched

- `src/Renderer/OpenGlRendererBackend.{hpp,cpp}` and every shader. The flat arrays stay the GPU hot
  path, unchanged - that is the whole reason this task is affordable.
- `src/Domain/`, `src/IO/`, `src/App/`, `src/ScientificRuntime/`.
- `src/Renderer/StructureRendererDataBuilder.{hpp,cpp}` - the domain to flat snapshot is out of scope.
- `src/Renderer/Scene/IsosurfaceMesher.*`, `src/Renderer/Scene/SelectionHitTest.*`.
- Anything under `docs/work/project/plans/`.

## Explicitly deferred, with the reason

- **Style presets.** A named, shared style library has no consumer yet: arrows and labels each own
  their style inline, and a single-slot copy/paste clipboard already exists. Add it when a second
  object kind needs to share a style, not before.
- **Project persistence and deterministic serialization.** Scene arrows and free labels are
  renderer-local today and the workstream file says renderer-local first. Serializing an object model
  that is about to grow four more kinds is work done twice.
- **The properties panel type dispatch.** Its five hardcoded blocks keep working, against ids instead
  of indices. Collapsing 1100 lines into a per-kind editor registry is a real refactor and belongs in
  its own branch straight after this one - folded in here, neither change stays reviewable.
- **Scene-space vs diagram-space switching.** Deferred by the workstream file itself.

## Acceptance criteria

1. A test creates a window with atoms plus one arrow and one free label, records every
   `SceneObjectId`, triggers `SyncSceneWithStructure`, and asserts every id is unchanged and still
   resolves to the same object. This is the property the whole task exists for.
2. A test asserts `registry.view<SceneObjectComponent>()` enumerates every atom, bond, pinned
   measurement, free label and scene arrow exactly once, each with the right `SceneObjectKind`.
3. A test asserts a destroyed object id never resolves again and is never reused.
4. A test selects one arrow, deletes a different arrow, and asserts the selection still refers to the
   originally selected arrow - the index-shift bug the current code has.
5. Ids survive an undo/redo round trip: the shared label/arrow undo stack snapshots whole vectors, so
   a restore must not renumber anything.
6. Manual run: outliner tree, arrow gizmo, label drag, region select and undo/redo all behave as
   before. Tests do not catch "the gizmo stopped responding".
7. `DefectStudioTests.exe` green in Release with the expected 2 skips.
8. `architecture-boundary-review` clean.

## Constraints

- `entt` stays a mirror. Do not make the registry the renderer data source, and do not add any render
  path that reads it.
- `SceneSystem` remains the only code that creates or destroys entities - its own header says so.
- Only the main thread mutates scene state.
- `.cpp` files stay under ~500 lines. `SceneOutlinerPanel.cpp` is already at 576 and
  `ObjectPropertiesPanel.cpp` at 1100, so any block touched there comes out into its own file.
- Run `scripts\Windows\GenerateProjects.bat` after adding sources.

## Reference

- `src/Renderer/Scene/SceneSystem.cpp` - `SyncSceneWithStructure` (13-54) and `SyncLabelEntities`
  (184-204) are the only entity lifecycle code in the repo; the arrow and free-label mirror is a
  third function in the same shape.
- `src/Renderer/RendererWindowState.hpp:183-192` (`SceneArrow`), `:249-263` (the shared label/arrow
  undo snapshot).
- `src/Core/Utils/Uuid.hpp`, if a `SceneObjectId` ever needs to be globally rather than per-window
  unique. Per-window monotonic is enough for everything in this task and is cheaper to debug.
