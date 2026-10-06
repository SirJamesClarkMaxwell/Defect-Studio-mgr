# Task 78: H on vacancies, select-all/"Wszystko" with orbitals, smooth orbitals, defect-axis orientation

Branch `task/70-operator-redo-panel` (HEAD `b435586`). Two other codex agents work in the same
tree at the same time:
- task 76: vacancy bonds; `ViewportVacancyAdd.*`, `OpenGlVacancyRenderer.cpp`, `isosurface.frag`,
  `PathBindingResolver.cpp`;
- task 77: curved arrows and path picking; `ScenePathCurvedArrow.*`, `src/Presentation/Operators/**`,
  `CurvedArrowParameters.hpp`, `src/Renderer/Path/ScenePathPicking.*`, `PathPicking.*`.

Do not touch their files; list every file you change.

## 1. H must hide selected vacancies

User: "with vacancies selected, H does not work".
- `VacancySite::hidden` exists and the outliner eye toggles it with a `SetVacanciesPayload`
  (`SceneOutlinerRows.cpp:~475`). Make H (and the RMB "Hide" item, `RendererPanelContextMenu.cpp:163`)
  hide the selected vacancies as well, in the same undo step as the other hidden objects.
- Alt+H (unhide all) also unhides vacancies.
- Hidden vacancies drop out of the selection.

## 2. Select all and the "Wszystko" mode with orbitals

User: "in select-all mode something still doesn't work with selection, especially with
orbitals".
- `RendererLayer::onSelectAllRequested` (`RendererLayer.cpp:2449`) only selects atoms, bonds and
  labels. Make it select every visible, pickable object of the active scene that the mode
  allows: atoms, bonds, vacancies, labels, scene orbitals, planes, paths and the defect axes if
  that is selectable.
- Check the "Wszystko" mode (Ctrl+4, `ViewportSelectionModeEntries` in
  `ViewportVerticalToolbar.cpp`), whose flags cover atoms, bonds and labels only. Clicking,
  shift-click, box and circle select must work on orbitals, planes, paths and vacancies too.
  Typical problems to check: a click on an orbital lobe selects the atom behind it; an orbital
  can't be added with shift; box select ignores orbitals.
- Ctrl+A toggles: a second press, or Alt+A, deselects all.
- Report what was broken.
- Curved-arrow/path picking precision is task 77; just make sure paths take part in select all
  and box select.

## 3. Orbitals shade smooth by default

User: "orbitals should have shade-smooth on automatically so they look better at lower
resolution".
- Make scene orbital (and LCAO orbital) meshes use smooth per-vertex normals by default. This
  covers existing orbitals in saved projects too (rendering default, not a per-object flag),
  unless a per-object flag already exists; then default it on and keep it saved.
- Do not change the isosurface shader (task 76 owns `isosurface.frag`). Compute normals on the
  CPU mesh side, or from the analytic gradient if the mesher has it.

## 4. Everything transforms in the defect axes by default

User: "what transforms along the defect axes? I think everything should, by default, once we
add them".
- When defect axes are created (any "Osie defektu (empty)" entry), switch the window's
  `transformOrientation` to `TransformOrientation::Defect` automatically. Switch back to Global
  when they are deleted.
- Make sure G/R/S with X/Y/Z constraints, the gizmo and numeric input use the defect axes for
  EVERY object kind: atoms, vacancies, orbitals, planes, paths, labels.
- Report a table of which kinds did and did not before the fix.
- The existing menu toggle "Gizmo i G/R/S w osiach defektu" stays as the manual switch.

## Tests

- H/Alt+H on vacancies with undo.
- Select all includes each object kind and respects hidden objects and the mode.
- The orientation default switches on creation and off on deletion.
- A per-kind constrained-move test in defect axes for at least an orbital, a plane and a path.
- Smooth normals: a unit test on the mesh normals, e.g. adjacent-face normals are averaged.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`,
`src/Domain/**`, `src/Core/Undo/**`, nor the task 76/77 files above. Polish UI strings with
diacritics. Report in `docs/work/project/tasks/78-selection-hide-orbitals-orientation-report.md`.
