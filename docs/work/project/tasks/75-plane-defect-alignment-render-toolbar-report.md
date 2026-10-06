# Task 75 implementation report

Implemented in the existing `task/70-operator-redo-panel` working tree. Changes are uncommitted.
Read task 74's report first and preserved its vacancy renderer, curved-arrow parameters,
operator redo path, curve catalogue and bond-radius propagation. No build, project generation,
C++ test execution or live rendering check was performed, per this task's instructions.
All shell operations used PowerShell built-ins; no Git Bash utilities or external executables ran.

## Changes and decisions

### Plane placement and centre-only moves

- **Osie defektu** and the plane N panel, for single and multiple selections, now offer
  **Ustaw w osiach defektu** with `płaszczyzna xy (⟂ z)`, `płaszczyzna xz`, and `płaszczyzna yz`.
  Every selected plane moves to the defect origin and gets the defect-frame normal/tangent.
- Mapping is xy: normal z, tangent x; xz: normal y, tangent x; yz: normal x, tangent y.
  Bitangent remains normal cross tangent (xz therefore has bitangent -z). Size, colour,
  opacity, border, visibility and stable ID survive. Invalid/nonfinite frames are rejected.
- **Anchored planes detach.** The tooltip explicitly says **Odczep** and explains that Ctrl+Z
  restores the complete operation. Anchor refresh cannot overwrite the requested placement.
- The plane Add section offers the same three presets when a defect frame exists. It uses
  `MakeDefaultScenePlane` for today's scene-relative size, places it at the defect origin,
  applies the same preset helper and selects the newly allocated object.
- **Przenieś środek na środek defektu** is separate from orientation. It appears in the defect
  menu and plane, orbital and path N panels. It translates each eligible selected object
  independently using the existing scene translation helpers; it does not rotate or resize it.
- Two-centre orbitals move by their midpoint, preserving the inter-centre axis and separation.
  Atom-anchored planes and orbitals detach. Free labels are also supported from the defect menu;
  their anchors remain and their offsets update through the existing label translation helper.
- Only explicitly selected objects move; selecting the defect axes does not implicitly move
  their unselected children. Atoms, vacancies and the defect frame itself are not targets.
- Paths with a free object transform can move their authored origin, including node-bound paths.
  Node bindings keep their existing world-space semantics. BondFrame paths have an atom-owned
  origin and are disabled for this action rather than accepting a move the resolver overwrites.
  LCAO orbitals and pinned measurements are also excluded. The tooltip explains these cases.
  Path object placement requires leaving path Edit Mode.
- Each clicked preset or centre move captures one scene-object snapshot for the whole batch
  and pushes one existing global undo command. There is no new undo system or persistence format.

### Plane orientation and Polish strings

- Added **Obrót XYZ (°)** to the plane N panel, including multi-selection. It converts the
  tangent/bitangent/normal frame to Euler degrees and writes it back through a quaternion,
  using the same convention as scene orbitals and paths. Conversion follows the
  [official GLM quaternion documentation](https://glm.g-truc.net/0.9.9/api/a00663.html).
- Typed angles remain in ImGui storage while dragging, so crossing the canonical yaw range
  does not reset the current edit. A multi-selection edit applies one common rotation.
- Manual rotation is disabled if any selected plane is atom-anchored; detach first or use a
  defect preset. Invalid/nonfinite rotation input leaves the object untouched.
- **Normalna** is retained as a readonly diagnostic line; raw normal editing was removed.
- Corrected the plane labels and hints: Płaszczyzny, Płaszczyzna, Środek, Połowa rozmiaru,
  Przezroczystość, the common-fields message and anchoring hint. Axis alignment combos now
  say **Oś obiektu** and **Oś defektu**. The defect-menu plane hint also has diacritics.

### Transparency and selection frames

- Planes now render after vacancy markers, scene orbitals, debug isosurfaces and both orbital
  isosurface channels, before the existing label and path-selection overlays.
- A pure helper sorts visible planes back to front by squared centre distance from the camera.
  It accounts for `sceneOffset`, retains scene order for equal distances and omits nonfinite
  centres. Distances use doubles to avoid float overflow during sorting.
- Both plane fill and selection-frame draws explicitly disable depth writes. Depth testing
  remains enabled, so opaque foreground geometry still occludes the appropriate plane regions.
  Geometry behind a plane is already present in colour/depth when the plane blends over it.
- Selection frames draw after all plane fills and are two-sided, preserving readability from
  either face instead of using front-face culling intended for closed isosurface shells.
- The shared GPU overlay helper adds `writeDepth` and `twoSidedOutline` parameters. Defaults
  preserve the previous orbital/isosurface behaviour. It restores the incoming depth mask
  after every draw; shader-missing returns occur before changing that mask. No exceptions added.
- The task's requested whole-plane centre sorting is used; intersecting translucent planes
  retain the usual limitations of sorting whole surfaces rather than individual fragments.
- Task 74's vacancy shader and marker rendering are untouched.

### Vertical toolbar and Add catalogue

- Arrow popup draws only **Rysuj** items: segments, free arrows, curved arrows and Krzywa.
- Plane popup draws only fitted/free planes and the defect presets.
- Orbital popup draws only the existing orbital catalogue, without another wrapper submenu.
- `DrawSceneAddMenu` has a section selector with **Full** as its default. Drawing and plane
  sections are factored out once, and the existing orbital section is reused. RMB and Shift+A
  compose the full menu from those same sections; there are no duplicate item lists.
- Polish tooltips retain available shortcuts: T, Shift+T, M/Shift+M, G/R/S, B/C and Ctrl+1..5.
  Add buttons mention Shift+A as the full-menu shortcut. Controls without a bound shortcut
  describe their action without inventing one. Selection-mode names are Polish too.
- No tools, icons, dependencies or new selection modes were added.

## Files changed

- src/Renderer/Scene/ScenePlanePlacement.hpp **(new)**
- src/Renderer/Scene/ScenePlanePlacement.cpp **(new)**
- src/Renderer/Scene/SceneAxisAlignment.hpp
- src/Renderer/Scene/SceneAxisAlignment.cpp
- src/Renderer/OpenGl/OpenGlRendererBackend.hpp
- src/Renderer/OpenGl/OpenGlRendererBackend.cpp
- src/Renderer/OpenGl/OpenGlScenePlaneRenderer.cpp
- src/Presentation/Panels/SceneOrientationControls.hpp
- src/Presentation/Panels/SceneOrientationControls.cpp
- src/Presentation/Panels/ViewportAddMenu.hpp
- src/Presentation/Panels/ViewportAddMenu.cpp
- src/Presentation/Panels/ViewportDefectFrame.cpp
- src/Presentation/Panels/ViewportVerticalToolbar.cpp
- src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp
- src/Presentation/Panels/ObjectPropertiesPanelSections.cpp
- src/Presentation/Panels/ObjectPropertiesPanelPathSection.cpp
- tests/Renderer/Scene/ScenePlanePlacementTests.cpp **(new)**
- tests/Presentation/Panels/ViewportVerticalToolbarTests.cpp
- docs/work/project/tasks/75-plane-defect-alignment-render-toolbar-report.md

No edits to Vendor, install/users, Domain, App, Core/Undo or Renderer/Path. New implementation
is 82 lines; new tests are 289 lines. ViewportDefectFrame.cpp was already approximately 500 lines
and receives one menu call plus a spelling correction. Other edited panel files remain below 500.
PowerShell edit/verification scripts and logs are under `tmp/task75-*`.

## Tests and validation

Added ten `ScenePlanePlacementTests` covering:

- xy/xz/yz normal/tangent and origin for a rotated defect frame, default size/style preservation;
- multi-selection, stale IDs, unselected planes, detachment and complete batch undo/redo;
- missing/invalid/nonfinite frames and invalid preset values;
- Euler/frame round trips from angles and all presets, wrapped angles and near-gimbal views;
- invalid Euler input, degenerate frames and anchored-plane rejection;
- back-to-front ordering, opposite camera positions, off-axis distance, stable ties, hidden planes,
  nonfinite centres/eyes and empty lists;
- centre-only plane moves preserving orientation and ignoring unselected parented children;
- two-centre orbital translation preserving axis, separation and Euler rotation;
- free path origin moves preserving rotation, scale and nodes, label anchor offsets, batch undo/redo;
- exclusion of atom-owned BondFrame origins.

Updated the existing toolbar catalogue test for its Polish names.

**Executed successfully:** PowerShell source checks for 18 changed source/test files, covering
encoding/conflict markers, balanced braces, permitted paths, render order, overlay defaults/state
restoration, all three toolbar routes, one curved-arrow item list, task 74's creators/redo path,
preset/rotation controls and Polish labels. Also passed 38 independent double-precision arithmetic
spot-checks for rotated presets, Euler/frame round trips, opposite-view distance sorting and
two-centre translation. Logs: `tmp/task75-verification.log`, `tmp/task75-math-verification.log`.

**C++ tests have not been compiled or run.** The source and arithmetic checks do not replace the
caller's build or visual verification.

## Caller follow-up

Premake regeneration is required for the new application/test translation units:

- src/Renderer/Scene/ScenePlanePlacement.cpp
- tests/Renderer/Scene/ScenePlanePlacementTests.cpp

The accompanying new header is src/Renderer/Scene/ScenePlanePlacement.hpp.
Set `DS_TOOLSET=msc-v143`, run `scripts/Windows/GenerateProjects.bat`, then build DefectStudioTests
and the application. Suggested focused filter:
`ScenePlanePlacementTests.*:SceneAxisAlignmentTests.*:ViewportSelectionModeEntriesTests.*`.

Visually check planes against atoms/bonds, translucent/solid vacancy markers, scene orbitals and
both orbital channels, from front/back/oblique angles in perspective and orthographic views.
Include overlapping planes, reverse camera views and selected outlines with border on/off.
Check single/multi anchored planes through all presets, centre-only moves, Ctrl+Z/redo and save/reopen.
Check that arrow/plane/orbital toolbar popups contain only their sections, while RMB/Shift+A retain
all task 74 items and the new plane presets.

## Knowledge graph and documentation access

Queried the saved graph with PowerShell before source navigation. Seed nodes were
DrawScenePlaneEditor, DrawDefectFrameMenu and DrawSceneAddMenu, with their one-hop relationships;
then read the scoped source because the graph did not contain implementation detail.
No graphify CLI or AST refresh ran: `graphify update .` requires an external executable and conflicts
with this turn's explicit PowerShell-built-ins-only constraint. The graph remains stale; refresh it
with `graphify update .` outside this constrained turn. This supersedes the general update rule for
this session only. Graph traversal/API cost: zero API tokens.

Context7 was not exposed among available tools. Used the official GLM documentation for quaternion
conversion; no Vendor sources were read for API questions.
