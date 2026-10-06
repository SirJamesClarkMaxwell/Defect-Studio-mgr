# Task 68: path feedback round 2 (ends, Ctrl+R, curved arrow, 3D shading)

Branch `task/65-feedback-round`. Another codex agent (task 69) and the caller edit other files in the
same working tree at the same time: touch only what this task needs and list every file you change.

The user's screenshots are attached (image order: 22 = object mode, 23 = the same path in edit mode).

## 1. One end of a curved ribbon arrow is wrong (images 22, 23)

A Bezier ribbon arrow (rectangular/bevelled cross-section, colour gradient blue to red, arrowhead at
the red end). At the START end (blue, no decoration expected) the object-mode render shows a thin
spike / notch sticking out of the tail (a sliver of geometry pointing back along the curve), while
in edit mode (image 23) the same end is a plain flat cap. Find the cause (start cap, bevel closure at
the start, tail decoration, ribbon end-taper, or the start handle pointing backwards) in the solid
mesher (`src/Renderer/Path/PathSolid*`, `PathTessellator`, `PathDecoration*`) and fix it so the tail is
a clean cap in every mode. Add a regression test that meshes such a path (curved cubic, start
handle at a sharp angle, ribbon with bevel, no start decoration) and asserts that no start-cap vertex
lies behind the start node along the start tangent by more than a small epsilon (or whatever
invariant captures the artefact you found).

## 2. Ctrl+R loop cut is not discoverable / does not work for the user

The user says Ctrl+R "does not exist yet". Today it only works in path edit mode (Tab) and only if the
cursor is exactly on a segment at the moment Ctrl+R is pressed - otherwise
`HandleViewportPathInsert` (`src/Presentation/Panels/ViewportPathInsert.cpp`) cancels silently.
Make it behave like Blender's loop cut:
- After Ctrl+R the insert mode stays active; every frame it re-picks the segment nearest to the
  cursor (use a generous pick distance; if nothing is near, keep the last segment or show nothing,
  but do not cancel). The preview dots follow the hovered segment; the wheel (and +/- / PageUp/Down)
  changes the count (min 1, cap it e.g. at 64); LMB / Enter confirms; Esc / RMB cancels. Show a small
  hint text near the cursor: "Wstaw N węzłów - kółko: ilość, LPM: zatwierdź, Esc: anuluj".
- Ctrl+R with exactly one path selected in object mode enters edit mode for it and starts the same
  modal (Ctrl+R is otherwise free in object mode; check keybindings.yaml and the dynamically
  registered binding in `ScenePathEditCommands.cpp` - do not edit `install/users/**`, register the
  binding in code like the existing one does).
- Splitting must not change the geometry: cubic segments by de Casteljau at equal parameter steps,
  line segments by linear interpolation, arcs by angle. Check `InsertNodes` (PathTopology) and
  `InsertScenePathNodes` (`src/Renderer/Path/PathInsertCommands.cpp`) and add a test that samples the
  path before and after inserting 3 nodes in a cubic and an arc segment and compares positions
  (max deviation < 1e-4).
- Add a menu item "Podziel segment (Ctrl+R)" to the path edit-mode context menu (RMB) if there is one.

## 3. Curved arrow between two atoms (C_n symmetry)

New add action "Zakrzywiona strzałka (C_n)" for exactly two selected atoms (or atom/vacancy ends,
like `AddScenePathThroughSelectedAtoms` in `src/Presentation/Panels/ScenePathOperations.cpp`): an arc
arrow from atom A to atom B that rotates around an axis, to show a C_n rotation. Axis: the defect
frame z axis through the frame origin when `structure.defectFrame` exists, else through the selected
vacancy (if one is selected besides the two atoms), else the axis perpendicular to the plane
(A, B, centroid of the structure's nearest neighbours) through the midpoint - pick the simplest sound
rule, document it in a comment. The arc lies in the plane perpendicular to the axis, its radius is
the mean distance of A and B from the axis, and it goes the short way from A to B; both ends are
bound (CopyPosition with the current path atom buffer) so the arrow follows the atoms if possible
(if an arc segment cannot keep bound ends, use a cubic Bezier that approximates the circular arc,
k = 4/3 tan(theta/4)). End decoration = arrowhead, like the existing "Arrow" through atoms. Give it a
public function `Result<SceneObjectId> AddCurvedArrowThroughSelectedAtoms(RendererWindowState&)` in
`ScenePathOperations.{hpp,cpp}` (that exact signature). Do NOT add menu items for it: task 69 unifies
the Add menus (`RendererPanelOrbitalMenu.cpp` etc.) and wires it in.
Test: two atoms at the same distance from the z axis, 120 degrees apart, give an arc whose midpoint
is at that same distance from the axis.

## 4. Paths do not look 3D (shading)

User: "the 3D arrows do not look as good as they could; it is hard to see that they are 3D and occupy
space". Compare the path/solid shader with the atom/bond shaders (`src/Renderer/OpenGl/Shaders/`)
and the path render pass in `OpenGlRendererBackend` / path renderer. Make the path solids shaded like
the bonds/atoms: proper per-vertex normals (smooth along the tube/ribbon, hard on cap and bevel
edges), the same light direction, ambient/diffuse/specular (Blinn-Phong) terms and the same
material settings that bonds use; gradient colour is the base colour. Check also that paths are
depth-tested against atoms (in the user's hBN screenshot a path line that starts at an atom centre
was drawn on top of the atom sphere - verify whether that is a depth issue or the line simply leaves
the sphere; fix it if it is a depth/ordering bug). Keep the edit-mode overlay unchanged.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`, `src/Domain/**`,
`src/Presentation/Panels/ViewportVacancyAdd.*`, `src/Presentation/EditorLayer*`,
`src/IO/SceneObjects*`, `src/Presentation/Panels/SceneOutliner*`,
`src/Presentation/Panels/RendererPanelContextMenu.cpp`, `src/Presentation/Panels/ObjectProperties*`
(other agents). Report: files changed, tests added, the cause of item 1, decisions.
