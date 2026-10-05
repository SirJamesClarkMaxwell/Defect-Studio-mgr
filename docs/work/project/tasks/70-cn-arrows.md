# Task 70: softer, flatter C_n arrows; arrows for many selected atoms

Branch `task/65-feedback-round`. The caller edits other files in the same working tree at the same
time (vacancy hiding): touch only what this task needs and list every file you change.

Two screenshots are attached: first = what `AddCurvedArrowThroughSelectedAtoms`
(`src/Presentation/Panels/ScenePathOperations.cpp`) makes today, second = the look the user wants
(a C_3 group diagram: e -> a -> a^2 -> e).

## 1. Softer, flatter arrow

User: "it should be a softer arrow, less curved at the ends, flatter in a way". Today the arrow
follows the full circle around the rotation axis (radius = atom distance from the axis), so a 120
degree step is a big half-loop with ends that leave the atoms almost tangentially. Wanted (second
image): a gentle arc between the two atoms that bulges outward (away from the axis) only a little,
like a circular arc whose sagitta is about 0.12-0.15 of the chord, leaving and entering each atom
roughly along the chord (a little outward), starting and ending at the atom surface with the usual
path atom buffer (both ends stay bound with CopyPosition / CopyVacancy as now). Make the bulge one
named constant (a "flatness" factor, e.g. the arc spans half of the rotation angle) so it can be
tuned. The arc still turns in the rotation sense from A to B around the axis.
"Softer": the arrow should look lighter than today - a thinner shaft and a smaller, slimmer
arrowhead than the default "Arrow" path (similar proportions to the second image). Use the existing
style fields of the path (width, end decoration size/shape); do not add new renderer features.

## 2. Many selected atoms: recognise the arrow pairs

User: "when selecting many atoms at once it should recognise between which ones the arrows go".
With N >= 3 selected atoms (optionally plus one selected vacancy, which then only defines the
axis), make a closed cycle of N arrows: sort the atoms by angle around the rotation axis and add
an arrow from each atom to the next one in the positive (right-handed about the axis) sense, the
last one back to the first. Typical case: the 3 carbons around a vacancy, C_3. Axis: the defect
frame z (through its origin) when `structure.defectFrame` exists, else through the selected
vacancy along the normal of the best-fit plane of the selected atoms, else the normal of the
best-fit plane through the atoms' centroid. With exactly 2 atoms keep today's single arrow A -> B
(same axis rules as now), but with the new flatter shape. All arrows of one call are one undo step
and become the selection. Change the return type as you see fit (e.g. the list of new ids) and
update the caller in `src/Presentation/Panels/ViewportAddMenu.cpp` (enable the item for >= 2
atoms, tooltip text in Polish with diacritics). Keep the function in `ScenePathOperations.cpp`
or split it into a new `ScenePathCurvedArrow.{hpp,cpp}` if that file gets near 500 lines.

## Tests

Update `tests/Presentation/Panels/SceneCurvedArrowTests.cpp`:
- 2 atoms 120 degrees apart around z: one arrow, its curve midpoint lies outside the chord
  (farther from the axis than the chord midpoint) by about the chosen sagitta, and much closer to
  the chord than the old circular arc.
- 3 atoms at 0, 120, 240 degrees around z (given in a shuffled order): three arrows, each joins
  consecutive atoms in the positive sense, one undo snapshot.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`, `src/Domain/**`,
`src/IO/**`, `src/Presentation/Panels/SceneOutliner*`, `src/Presentation/Panels/ViewportVacancy*`,
`src/Renderer/OpenGl/**`, `src/Renderer/StructureRendererDataBuilder*`, `src/Renderer/RendererTypes.hpp`,
`src/Presentation/EditorLayer*`. Report in `docs/work/project/tasks/70-cn-arrows-report.md`: files
changed, tests, the chosen flatness and style values, decisions.
