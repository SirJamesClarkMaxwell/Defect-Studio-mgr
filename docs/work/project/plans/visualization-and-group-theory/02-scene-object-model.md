# 2. Shared visual-object model

Branch: `task/20-scene-object-model` · Source: locked plan points 1 and 2

## Scope

- Extend the existing `SceneArrow`/renderer annotation flow, `ObjectPropertiesPanel`, quick-edit,
  Scene Outliner, renderer, and local annotation undo.
- Keep the first rework renderer-local; add project persistence as a separate follow-up stage once
  the object model is stable.
- Keep geometry/data separate from appearance and use shared style presets with per-object
  overrides.
- Support standalone objects and optional semantic links to atoms, structures, operations, or
  electronic-structure sources.
- Preserve stable IDs, provenance, deterministic serialization, visibility, and stale/regeneration
  state.
- Let one object switch between scene-space 3D and diagram/screen-space presentation without
  duplicating the object.
- Hide coordinate-system details from the user; expose comfortable manipulators and numeric
  properties.
- Reuse the current renderer and PNG/JPG export path.

## Reuse in this repo

- `src/Renderer/Scene/SceneComponents.hpp` (71 lines) already holds `TransformComponent`,
  `AtomComponent`, `BondComponent`, `VisibilityComponent`, `SelectionComponent`,
  `CollectionComponent`, `LabelComponent` over an `entt` registry. This IS the visual-object model —
  extend it, do not start a parallel hierarchy.
- `SceneRegistry.hpp`, `SceneSystem.{hpp,cpp}`, `Entity.hpp`, `SelectionHitTest` complete the ECS.
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp` (1100 lines) and `SceneOutlinerPanel` are the
  existing edit surfaces.

## Deferred within this workstream

Scene-space ↔ diagram/screen-space switching doubles the complexity of every object before one
exists. Ship scene-space only; diagram-space is its own later task.
