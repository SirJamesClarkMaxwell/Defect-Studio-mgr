# 4. Arrows and paths

Source: locked plan point 3

## Scope

- Represent a path as ordered control points and piecewise quadratic/cubic Bézier segments.
- Keep path geometry separate from physical interpretation.
- Make arrows separate objects that reference a path, so one path can render as a trajectory, curved
  arrow, vector series, or animation.
- Expose line pattern, width, opacity, color, border/stroke, head shape/size, and independent
  start/end heads. Support dashed symmetry axes and one-ended arrows.
- Use the existing SceneArrow rework plan as the implementation base; do not create a parallel
  annotation system.

## Reuse in this repo

`SceneArrow` already spans `ViewportSceneArrowGizmo`, `ViewportSceneArrowInteraction`,
`SceneArrowEditorWidget`, `SceneOutlinerPanel`, `ObjectPropertiesPanel`, `RendererPanelToolbar`,
`OpenGlRendererBackend`, `RendererLayer`, `RendererMeshData` and local undo.

## Deferred within this workstream

Bézier curvature is not needed for the NV⁻ acceptance case — straight arrows and dashed axes cover
it. Add curves when a figure actually requires one.
