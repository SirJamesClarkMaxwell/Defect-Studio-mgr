# Task 64: path S16 - delete the legacy SceneArrow runtime

Plan: `docs/work/project/plans/2026-09-20-path-system-implementation.md` S16. Inventory:
`docs/work/project/plans/path-legacy-inventory.md` - re-run its grep first
(`SceneArrow|sceneArrows|selectedSceneArrows|sceneArrow` in `src` and `tests`), the footprint moved in
tasks 59-63. After task 63 (cutover) nothing creates, saves or loads a runtime SceneArrow any more.

## Goal

The runtime legacy arrow is gone: `RendererWindowState::SceneArrow`, `sceneArrows`, `selectedSceneArrows`,
quick-edit / drag / gizmo-handle state, `SceneArrowGeometry`, `SceneArrowEditorWidget`,
`SceneArrowOperations`, `ViewportSceneArrowInteraction`, the arrow render pass and the Arrow2D quad shader
(`arrow_quad.*`), `SceneObjectKind::SceneArrow`, the arrow slice of the scene-objects snapshot, arrow
transform targets (`SceneArrowTransformTarget`, `ArrowTransformStart`), arrow visibility, arrow clipboard,
and the legacy tests that test only that code.

What STAYS (inventory class S): the v1 DTO `PersistedSceneArrow`, its YAML parse, `MigrateArrowToPath`,
the v1 fixtures and their parse/migration tests, `PathStyle.hpp`'s legacy tip mapping - old projects must
still load. Also keep `m_ConeMesh` (displacement arrows draw it) and anything displacement-arrow related.

## Rules

- Delete, do not wrap. No compatibility shims left in runtime code.
- A test that covers behaviour paths also have is ported to paths only if no path test covers it yet;
  otherwise delete it with the code.
- Comments that mention the legacy arrow as a live thing are reworded or removed (the inventory lists the
  comment-only references).
- Gate: after the change the grep finds the legacy names only in the v1 DTO / parser / migration / fixtures
  / their tests and in docs. List any other hit in your report with the reason it stays.
- Update the inventory file's header with the date and "S16 done" and the plan's S16 status line.

## Files that must NOT be touched

`Vendor/**`, `install/users/**`, `src/Domain/**`, `src/Renderer/Path/PathSolid*`, `PathStrokeMesher*`,
`PathDecorationMesher*` (geometry is not part of this task).

## Acceptance

1. The caller's full Release build of DefectStudio and DefectStudioTests succeeds, all tests pass (1 skip).
2. In the app: paths still create/select/transform/delete/undo; a v1 project with arrows still opens as paths.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build in your sandbox;
the caller builds and sends you errors. Because this deletes a lot, be systematic: for every deleted
declaration search every remaining reference before finishing.
