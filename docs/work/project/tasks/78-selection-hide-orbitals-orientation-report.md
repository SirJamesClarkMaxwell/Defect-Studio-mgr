# Task 78 implementation report

Implemented selection/hiding, smooth scene-orbital normals, and defect-axis orientation defaults.
Changes are left in the working tree; no build or commit was performed.

## H / RMB Hide / Alt+H

- H and RMB Hide now include selected vacancies. They reuse the existing SetVacancies command,
  update the domain vacancy list, mark it modified for sidecar saving, rebuild all windows showing
  that structure, and discard hidden/stale vacancy selections.
- Vacancy edits, atom/bond visibility, and drawing-object visibility join one UndoScope.
  H previously omitted vacancies and could create separate atom and annotation undo steps.
- Alt+H unhides vacancies through the same domain command and groups the complete operation.
- Vacancy-only and bond-only selections now enable RMB Hide. Defect-axis visibility is included
  in the scene-object undo snapshot. Hidden drawing selections are cleared and the ECS mirrors synced.
- The vacancy child command executes directly inside the group: CommandRegistry rejects nested
  Execute calls, so calling the registry again from the H event handler would fail.

## Selection: what was broken

- Ctrl+A selected only atoms, bonds and labels, and included hidden labels. It now includes
  visible atoms, bonds, vacancies, pinned/free labels, usable scene/LCAO orbitals, planes,
  paths and shown defect axes. Global atom/bond/vacancy/axes draw switches are respected.
- Ctrl+A fills the eligible selection; pressing it again when all eligible objects are selected
  clears every kind. Alt+A clears the Object Mode selection independently of the current pick mask.
- The existing pickLabels flag already gates orbitals and planes as drawing objects; the
  "Wszystko" entry enables that flag. Its mask did not need extra booleans.
  Atoms follow pickAtoms, bonds follow pickBonds, orbitals/planes/labels follow pickLabels,
  vacancies/axes follow pickAtoms OR pickLabels. Paths follow their existing independent picker
  in all masks, matching the concurrent task 77 region/picker changes.
- Orbital clicks previously gave any atom on the ray priority, including an atom behind a
  visible lobe. The picker also used a large bounding sphere which covered empty lobe gaps.
  Picking now tests cached triangles from the same CPU geometry generator used by rendering,
  retains the sphere only for broad-phase rejection, and compares the hit against actual atom surfaces.
- Plane clicks also compare depth against atom surfaces instead of granting atoms unconditional priority.
- Shift-click adds orbitals, planes, paths and vacancies without clearing other kinds.
  Ctrl-click toggles; a plain click replaces. The defect-frame interaction follows the same additive rule.
- Box/circle selection previously omitted planes and used only an orbital's centre.
  It now overlaps orbital triangles and plane quads. Vacancy/axes centres participate,
  hidden labels are excluded, and the existing path region hit-test remains connected.
  Annotation click handlers no longer consume the start of a box/circle gesture.
  Path-region selection retains the existing requirement that render-derived path geometry is cached.

## Smooth orbital normals

CPU marching-tetrahedra meshing now computes finite-difference gradients at grid samples,
interpolates them at edge crossings, and normalizes the resulting per-vertex outward normals.
The inverse-transpose cell matrix handles the physical grid basis; positive and negative lobes
get the appropriate normal direction. The existing stretch normal transform is retained.

Both ordinary and LCAO scene meshes use this generator, including objects loaded from saved
projects. This is a rendering default, with no new saved flag. The isosurface shader is untouched.
Invalid dimensions, sample counts, thresholds and singular cells are rejected before meshing.

## Defect orientation and transforms

Creation through the shared SetDefectFrame command switches every affected window to Defect.
Deletion switches to Global and clears the deleted frame's selection/temporary parenting.
Undo/redo of creation/deletion follows those transitions. Editing an existing frame preserves a
manual orientation choice; "Gizmo i G/R/S w osiach defektu" remains available.

The gizmo, constrained G/R/S and numeric input already use the same SceneTransformBases and modal
transform implementation. The missing default and object-specific transform gaps were fixed:

| Kind | Before, with Defect manually chosen | After |
| --- | --- | --- |
| Atoms | Defect constraints worked | Same shared basis, now the creation default |
| Vacancies | Defect constraints worked | Same shared basis, now the creation default |
| Ordinary orbitals | G/R used the basis; constrained S changed uniform size | Defect defaults; axis S changes the corresponding authored stretch extents |
| LCAO orbitals | G/R explicitly ignored; only scalar S | G/R transforms components, detaching atom anchors; axis S uses stretch |
| Planes | Basis worked, but fitted anchors could overwrite G/R next frame | G/R detaches fitted anchors; cancel restores them |
| Paths | Defect constraints already worked | Same shared basis, now the creation default |
| Free/pinned labels | Shared constraints already worked | Same shared basis, now the creation default |

Text keeps its established billboard rotation and uniform glyph-size behavior. Ordinary orbital
rotation also detaches positional anchors so the preview is not overwritten.
Transform snapshots restore plane/orbital anchors and LCAO component positions/orientations on cancel.

Intentional ceiling: orbital stretch stores three authored extents, not shear. An off-axis
nonuniform scale is projected onto those extents. The ponytail comment identifies an affine
shape matrix as the upgrade if exact shear becomes necessary.

## Verification

Added 12 GTest tests in the two Task78 test files:

- Real hide/show commands with the registry execution guard active; vacancy + atom + label
  visibility in one undo step, redo, Alt+H undo, and hidden vacancy selection removal.
- Defect-axis creation/deletion orientation, undo/redo, manual-choice preservation, and axes-hide undo.
- Select-all covering every kind, hidden objects, mode filtering, repeated-toggle/explicit clear,
  and global draw switches.
- Rectangle/circle overlap with orbital/plane surfaces; subtraction and pick-mask exclusion.
- Numeric Defect-X constrained moves for orbital, plane, path, label, vacancy and atom.
- Anchored-plane persistence after movement/cancel, LCAO detachment/cancel, and constrained orbital scale.
- Coarse-grid gradient-normal direction, normal variation within a face, smooth scene/LCAO meshes,
  and surface picking in front of an atom versus empty bounding-sphere space.

PowerShell static checks passed: balanced braces, resolvable project includes, and the edit list
contains no prohibited paths. New C++ files and changed scene helpers remain below 500 lines;
the pre-existing RendererLayer and command-registration translation units already exceed that size.

Tests were **not built or run**, as explicitly required by the task. Project regeneration is also
pending because invoking GenerateProjects.bat conflicts with the PowerShell-built-ins-only restriction.
The caller should regenerate with DS_TOOLSET=msc-v143, build, and run
DefectStudioTests.exe --gtest_filter=Task78*.*, followed by the existing orbital, mesher,
transform and visibility suites and the full suite. Interactive Shift-click and box/circle checks
remain for the caller's built application.

Graphify JSON was queried first with PowerShell; it did not expose the required symbols.
The graphify update command was not run because its executable is outside the built-ins-only restriction.
The graph therefore needs an update when external native commands are permitted.
No third-party/Vendor sources were opened and no task 76/77 protected files were edited.

## Files changed by task 78

- src/Renderer/RendererLayer.cpp
- src/Renderer/RendererLayer.hpp
- src/Renderer/RendererWindowState.hpp
- src/Renderer/Commands/RendererCommandRegistration.cpp
- src/Renderer/Commands/RendererVacancyCommands.cpp
- src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp
- src/Renderer/Scene/IsosurfaceMesher.cpp
- src/Renderer/Scene/IsosurfaceMesher.hpp
- src/Renderer/Scene/SceneOrbitalGeometry.cpp
- src/Renderer/Scene/SceneOrbitalGeometry.hpp
- src/Renderer/Scene/SceneSelection.cpp
- src/Renderer/Scene/SceneSelection.hpp
- src/Renderer/Scene/SceneTransform.cpp
- src/Renderer/Scene/SceneTransform.hpp
- src/Renderer/Scene/SceneVisibility.cpp
- src/Presentation/Panels/ViewportInteraction.cpp
- src/Presentation/Panels/ViewportLabelInteraction.cpp
- src/Presentation/Panels/ViewportPicking.cpp
- src/Presentation/Panels/ViewportRegionSelect.cpp
- src/Presentation/Panels/ViewportSceneOrbitalInteraction.cpp
- src/Presentation/Panels/ViewportScenePlaneInteraction.cpp
- src/Presentation/Panels/ViewportScenePathInteraction.cpp
- src/Presentation/Panels/ViewportSelection.hpp
- src/Presentation/Panels/ViewportVacancySelection.cpp
- src/Presentation/Panels/ViewportVacancySelection.hpp
- tests/Renderer/Task78SceneInteractionTests.cpp
- tests/Renderer/Task78OrbitalNormalsTests.cpp
- src/Renderer/Commands/RendererVacancyCommands.hpp
- docs/work/project/tasks/78-selection-hide-orbitals-orientation-report.md

