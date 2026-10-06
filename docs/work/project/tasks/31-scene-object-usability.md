# Task 31: scene-object usability, round C

Fourteen items from the first hands-on test of `task/30-scene-objects-remediation`
(rounds A, B and task 28, commits `d7bb1a6`, `e7f04bb`, `bdef3a3`). Recorded as reported, then
triaged against the code. Nothing here is fixed yet.

## Triage summary

| # | Item | Verdict |
|---|------|---------|
| 3 | Planes cannot be selected in the viewport | Never built |
| 5 | Orbitals are hard to select and to deselect | Partly built, broken |
| 9 | Right-click context menu dead in the viewport | Regression |
| 10 | A giant orange plane appears after adding an arrow between two atoms | Bug, cause unknown |
| 6 | No keyboard navigation in the Scene Outliner | Never built |
| 14 | No gizmo for orbitals and planes | Never built |
| 4 | Orbitals should be added at every selected atom, not at the 3D cursor | Never built |
| 2 | Multi-selection cannot be edited | Never built (known, deferred earlier) |
| 1 | The N panel should be resizable | Never built |
| 7 | Outliner should use Blender's eye/camera icons, on the right | Known ceiling of task 28 |
| 8 | Vertical toolbar: add-tools at the bottom, select and 3D cursor at the top | Never built |
| 11 | An arrow between two atoms needs a buffer/offset parameter | Never built |
| 12 | The Add -> Orbital submenu needs rethinking | Never built |
| 13 | Orbitals/bonds between selected atoms with the right orientation, saved per material | Design question |

## Findings

### 3. Planes are not selectable (never built)

`RunViewportGizmoChain` (`ViewportInteraction.cpp:45-50`) chains pin, free-label, arrow and orbital
interaction. There is no `HandleScenePlaneInteraction`. `HandleViewportPick`
(`ViewportPicking.cpp:95`) ray-tests atoms and bonds only. So nothing in the viewport ever looks at
`scenePlanes` - selection works from the outliner row alone. This is the open question from round B
that was never answered.

### 5. Orbitals are hard to select and deselect (partly built)

`HandleSceneOrbitalInteraction` exists and is last in the chain, so every earlier handler gets first
refusal on the click. Needs a look at its hit test and at whether a click on empty space clears
`selectedSceneOrbitals` (Escape does - `RendererPanel.cpp:236-241`).

### 9. Right-click context menu dead (regression)

`renderViewportContextMenu` opens with `ImGui::BeginPopupContextItem`, which hangs off *the last
submitted item*, not off the viewport image - and by the time it is called
(`RendererPanel.cpp:267`) the drag-drop target, the gizmo chain and the label/arrow/orbital
handlers have all run and may have submitted items of their own. The fix is to stop depending on
item order: open the popup explicitly from a hover plus right-click test against the image rect and
use `BeginPopup`. The existing comment at `RendererPanel.cpp:576` already describes the same class
of drift for the popup's world position.

Also asked for: more entries in that menu now that there are more object kinds.

### 10. Giant orange plane after adding an arrow

Screenshot: adding an arrow between two atoms left a huge orange wedge across the structure, with a
small red arrow at the click point. Orange is the selection tint, so something plane-shaped is
being created or drawn at the wrong scale. Unknown whether the arrow geometry degenerates or a
plane object is created by accident.

### 6. Keyboard navigation in the Scene Outliner (never built)

Clarified by the reporter: the arrow *keys*, not arrow objects. Up/Down should move between rows,
Left/Right collapse and expand a group, Enter confirm the row under the cursor as the selection,
Escape clear the selection. Today every row is a plain `Selectable`/`TreeNodeEx` with no focus
model, so the panel is mouse-only.

### 4. Orbitals at every selected atom

`MakeDefaultSceneOrbital` (`SceneOrbitalGeometry.cpp:196`) anchors only when the selection is
*exactly* one atom (or exactly two for a two-center preset); anything else falls back to the seed
position, which is why it works for one atom and silently does the wrong thing for several. Wanted:
one orbital per selected atom, all at once.

### 2. Editing a multi-selection

`ObjectPropertiesPanel` refuses with "Select exactly 1 atom to edit its properties." Multi-select
editing was deferred in `docs/work/project/TODO.md` ("Replanning 2026-08-22"). The same limit
applies to the other object kinds. At minimum the properties shared by every selected object of one
kind should be editable together.

### 7. Eye/camera icons in the outliner

Task 28 shipped two checkboxes with tooltips and a `ponytail:` note saying real glyphs should
replace them. Wanted: Blender's eye and camera icons, and on the *right* edge of the row, not the
left. The toolbar already loads PNG icons through `RendererLayer::GetToolbarIcon`, and the existing
toolbar icons were generated with Pillow - the same route works here.

### 11. Arrow buffer parameter

`RendererWindowState::SceneArrow` (`RendererWindowState.hpp:219`) has `start`, `end`, `kind`,
`orientation2D`, `fixedPlane` and a style - no gap/inset. An arrow drawn between two atom centres
buries its ends inside the spheres.

### 13. Reusable orbital/bond sets per material (design question)

Two parts: (a) add orbitals or bonds between the selected atoms with the correct orientation in one
action, (b) store that set per material so it does not have to be redone for every cell. (b) is a
new persistence concept - where it lives (project file? a material record in the domain?) is a
decision, not an implementation detail.

## Not in this task

Persisting `visible`/`renderable` for scene objects (`src/IO/SceneObjectsIO.hpp`) - still the
follow-up noted in task 28.
