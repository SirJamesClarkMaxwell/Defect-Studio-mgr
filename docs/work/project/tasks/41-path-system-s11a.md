# Task 41 S11a: make paths clickable in the viewport

## Goal

S10 shipped `PickPath` as a library with zero callers: clicking a path in the viewport does
nothing, because nothing asks the picker and there is nowhere to store the answer. After S11a a
left click on a path in a viewport selects it, the selected path is visibly highlighted, and a
click on empty space deselects it - exactly the gesture arrows, orbitals and planes already have.

This is the front half of S11. The Outliner row, the Properties section, clipboard/delete/
duplicate and region select are S11 proper and are **out of scope here**.

## Files to create or change

Already written, and they are the contract - do not change a signature or a comment in them:

- `src/Renderer/Path/ScenePathPicking.hpp` (new)
- `src/Renderer/Path/PathCaches.hpp` - `FindLastBuilt` declaration only
- `tests/Renderer/Path/ScenePathPickingTests.cpp` (new)

To write:

1. `src/Renderer/Path/ScenePathPicking.cpp` (new) - `PickFrontmostScenePath`.
2. `src/Renderer/Path/PathCaches.cpp` - implement `FindLastBuilt`. Five lines; the same
   `std::find_if` as `Find`, without the key comparison.
3. `src/Renderer/RendererWindowState.hpp` - add next to `selectedScenePlanes`:
   `std::vector<SceneObjectId> selectedScenePaths;` with a comment saying it is the same
   multi-select shape and that `back()` is the anchor.
4. `src/Presentation/Panels/ViewportScenePathInteraction.cpp` (new) -
   `bool HandleScenePathInteraction(RendererWindowState &, const ImVec2 &imageOrigin,
   const ImVec2 &imageSize, bool hovered)`. Model it line for line on
   `ViewportScenePlaneInteraction.cpp`, which is the same handler for planes: same guard clauses,
   same unprojection against `imageSize` rather than `windowState.viewportSize`, same plain-click-
   replaces / Ctrl-click-toggles rule, same "claiming the click clears the other kinds" block at
   the end (and the other handlers' clear blocks must gain `selectedScenePaths` in return).
   Difference: paths pick in screen space, not with a ray, so build a `PathPickSettings` instead -
   `viewProjection` from the camera, `viewportSize` = the drawn `imageSize`, `cursor` = the
   mouse position relative to `imageOrigin`, `cameraRight` from column 0 of the view matrix the
   way `OpenGlPathRenderer.cpp:133` reads it, `editMode = false` (S12 owns edit mode).
5. `src/Presentation/Panels/ViewportSelection.hpp` - declare `HandleScenePathInteraction` with a
   comment in the style of the ones already there.
6. `src/Presentation/Panels/ViewportInteraction.cpp` - insert it into the short-circuit chain
   between `HandleSceneOrbitalInteraction` and `HandleScenePlaneInteraction`. A path is a thin
   object and a plane is the backdrop behind everything, so the path gets first refusal.
7. `src/Presentation/Panels/ViewportPicking.cpp` - `ClearAnnotationSelections` clears
   `selectedScenePaths` too. Without this an atom click never releases a selected path.
8. `src/Renderer/OpenGl/OpenGlRendererBackend.hpp` - `PathRenderInput` gains
   `const std::vector<SceneObjectId> *selected = nullptr;`, documented as "null or empty means
   nothing is highlighted".
9. `src/Renderer/RendererLayer.cpp:499` - fill the new field from
   `windowState.selectedScenePaths`. No index translation: unlike planes, paths are addressed by
   id all the way down.
10. `src/Renderer/OpenGl/OpenGlPathRenderer.cpp` + `src/Renderer/OpenGl/Shaders/path_stroke.frag` -
    highlight the selected paths. The stroke colour is baked into the vertex buffer at mesh-build
    time, so re-colouring per selection would rebuild the mesh on every click; add a
    `uniform vec3 u_SelectionHighlight;` plus `uniform int u_Selected;` to the fragment shader
    instead and set it per draw job. Compute the highlight colour on the CPU with the existing
    `ApplySceneSelectionHighlight` from `src/Renderer/Scene/SceneObjectAppearance.hpp` so a
    selected path looks like every other selected scene object, and mix towards it in the shader
    rather than replacing the colour outright (a gradient must stay legible while selected).
11. `src/Presentation/Panels/ViewportSceneArrowInteraction.cpp`,
    `ViewportSceneOrbitalInteraction.cpp`, `ViewportScenePlaneInteraction.cpp`,
    `ViewportLabelInteraction.cpp`, `ObjectPropertiesPanelOrbital.cpp`,
    `SceneOutlinerPanel.cpp`, `RendererPanel.cpp`,
    `src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp` - every place that clears the other
    four selection vectors together must also clear `selectedScenePaths`. Grep
    `selectedScenePlanes.clear()` and treat each hit as a checklist item; a missed one leaves a
    path selected while the Properties panel shows something else.

## Files that must NOT be touched

- `src/Renderer/Path/PathPicking.{hpp,cpp}` and `PathHandleGeometry.{hpp,cpp}` - S10 is verified
  and committed; if the picker looks wrong, say so, do not edit it.
- `src/Renderer/Path/ScenePathPicking.hpp`, `PathCaches.hpp` beyond the declaration already there,
  and `tests/Renderer/Path/ScenePathPickingTests.cpp` - the contract.
- Everything else under `src/Domain/`, `src/IO/`, `src/App/`, `src/ScientificRuntime/`.
- Any existing test file.
- `src/Presentation/Panels/SceneOutlinerRows.cpp`, `ObjectPropertiesPanelSections.cpp`,
  `SceneObjectEditActions.cpp`, `SceneOrbitalOperations.cpp`, `ViewportRegionSelect.cpp` - those
  are S11, not S11a. The only exception is `SceneOutlinerPanel.cpp`'s existing
  "clear the other selections" blocks, per item 11.

## Out of scope

- The Outliner row for a path, the Properties section, rename, visibility eye/camera columns.
- Clipboard, delete, duplicate, paste.
- Box/circle region select over paths.
- Edit mode, node dragging, the transform gizmo acting on a path.
- Atom/bond/plane vs path depth arbitration across kinds. The `||` chain already decides the
  order, the same way it does for every other annotation kind; `ScenePathPick::depth` exists so
  S11 can do better later, and nothing has to consume it yet.

## Acceptance criteria

`PathCaches::FindLastBuilt` and `PickFrontmostScenePath`, covered by
`tests/Renderer/Path/ScenePathPickingTests.cpp`:

1. `FindLastBuilt` returns the stored entry for a key that `Find` rejects, and nullptr for an
   unknown id.
2. An empty `PathSystem` picks nothing.
3. A cursor off every path picks nothing.
4. A cursor on the shaft picks that path, with `result.kind == WholePath` in object mode.
5. Of two overlapping paths the frontmost wins, in either store order.
6. `depth` is NDC, and the nearer hit reports the smaller value.
7. A path with `visible == false`, and one with `renderable == false`, are not pickable.
8. A path whose geometry was never cached offers no shaft.
9. A hidden path in front does not shadow a visible one behind it.
10. A degenerate viewport picks nothing.
11. With `editMode == true` the reported kind is the element kind, not `WholePath`.

Plus:

12. `scripts\Windows\GenerateProjects.bat` succeeds after the two new `.cpp` files.
13. Release build of `DefectStudioTests.vcxproj` and `DefectStudio.vcxproj` is clean.
14. The full Release suite passes with exactly the two known `DS_PYTHON_CAPI_AVAILABLE=0` skips.
15. `grep -rn "selectedScenePlanes.clear()" src/` and
    `grep -rn "selectedScenePaths.clear()" src/` return the same set of files.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `ScenePathPicking` is Renderer; the ImGui handler is
  Presentation and must not reach into Domain.
- `Renderer` is the exception-free zone: no `throw` in anything under `src/Renderer/`.
- No `std::thread` outside JobSystem.
- `.cpp` files stay under ~500 lines.
- `near` and `far` are Windows macros - never use them as identifiers.
- Do not tessellate in `ScenePathPicking.cpp`. Read the cache; the header says why.

## Manual round

Add a dev path (right-click > Path (dev) > Line / Cubic / Arc), click it: it highlights. Click
empty space: it releases. Ctrl-click a second path: both highlight. Click an atom: the path
releases.
