# Task 28: outliner visibility columns and the N side panel

## Goal

Every row in the Scene Outliner gets Blender's two visibility columns - an eye (drawn in the
viewport) and a camera (drawn in an exported render) - instead of only window and species rows
having a checkbox and labels, arrows and orbitals having nothing at all. H and Alt+H stop being an
atoms-only shortcut. And the object properties move within reach: N slides them in over the right
edge of the viewport, the way Blender's N panel does, rather than requiring a docked panel to be
open.

## Files to create or change

- `src/Renderer/Scene/SceneVisibility.cpp` - new, implements the four functions in the header of
  the same name. Pure state logic, no ImGui.
- `src/Presentation/Panels/SceneOutlinerPanel.cpp` - an eye and a camera toggle on every row:
  window, species, atom, label, pinned measurement, arrow, orbital. A parent row's toggle applies
  to everything under it and shows a mixed state when its children disagree. The header's comment
  saying labels and arrows have no visibility field is now wrong - update it.
- `src/Presentation/Panels/RendererPanel.cpp` - route the existing Hide/Show menu items and the
  H / Alt+H keys through `SetSelectedSceneObjectsVisible` / `ShowAllSceneObjects` so they cover
  every kind of scene object, not just atoms. Enabled state reads `AnySceneObjectSelected`.
- `src/Renderer/RendererLayer.cpp` - `PopulateExportPreviewState` calls
  `ApplyRenderPassVisibility` on the copy it fills, so the camera column actually decides what an
  exported image contains. This is the only place the render channel is consumed.
- `src/Renderer/Scene/SceneSystem.cpp` (or wherever
  `PushSelectionAndVisibilityToWindowState` lives) - carry `renderable` across the ECS/flat-array
  mirror alongside `visible`, so the two do not drift.
- `src/IO/SceneObjectsIO.hpp` is off-limits, but `visible`/`renderable` on the persisted structs is
  a follow-up, not this task: say so in your report rather than adding it.

### The N panel

- New `src/Presentation/Panels/ViewportSidePanel.{hpp,cpp}` - an ImGui child drawn inside the
  viewport panel's own rect, anchored to its right edge, full height, ~320px wide, hosting the same
  content `ObjectPropertiesPanel` draws. Toggled by N while the viewport has focus. It slides: an
  eased width animation over ~0.15s in and out driven by `RendererLayer::GetLastDeltaTime`, not an
  instant show/hide, and the viewport image under it is not resized - the panel floats over it.
- The panel's content must be the existing object-properties rendering, called from both places.
  If that means extracting it from `ObjectPropertiesPanel::Render` into a free function taking the
  window state, do that - what must not happen is a second copy of the property widgets.

## Files that must NOT be touched

- `src/Renderer/Scene/SceneVisibility.hpp`, `src/Renderer/RendererWindowState.hpp`,
  `src/Renderer/RendererTypes.hpp`, `src/Renderer/Scene/SceneComponents.hpp` - the contract. The
  `visible`/`renderable` fields are already declared on every struct that needs them.
- Everything under `tests/`.
- Anything under `src/Domain/`.

## Acceptance criteria

1. `tests/Renderer/Scene/SceneVisibilityTests.cpp` passes in full - nine cases, all currently
   failing because `SceneVisibility.cpp` does not exist yet.
2. Every other existing test still passes.
3. Every outliner row has both toggles, and toggling a window or species row updates its children.
4. Selecting a label, an arrow and an orbital together and pressing H hides all three; Alt+H brings
   them back.
5. Clearing an object's camera column leaves it on screen but omits it from an exported PNG.
6. N opens and closes the side panel with a visible slide, and editing a property there has exactly
   the same effect as editing it in the docked Object Properties panel.

## Constraints

- Layer boundaries in `AGENTS.md` are hard. `SceneVisibility.cpp` lives in `Renderer` and must not
  include anything from `Presentation`.
- `.cpp` files stay under ~500 lines. `SceneOutlinerPanel.cpp` is at 656 and
  `ObjectPropertiesPanel.cpp` at 1125 - both are already over. Do not make either worse; splitting
  the part you touch out into a sibling file is the expected move, followed by
  `scripts/Windows/GenerateProjects.bat`.
- `SceneVisibilityTests.cpp` is a new file, so the projects must be regenerated before it builds.
- Do NOT run a build or the tests - the MSBuild toolchain is not reachable from your sandbox.
