# Task 80: curved arrows - "Promień okręgu" must move the ends; reach the user's C_2 sketch; Ctrl+Z in the redo panel

Branch `task/70-operator-redo-panel` (HEAD `5eb406e`). Two other codex agents work in the same
tree at the same time:
- task 79: vacancy bonds/renderer; `ViewportVacancyAdd.*`, vacancy renderer,
  `OpenGlRendererBackend.cpp`, `isosurface.frag`;
- task 81: orbitals; `IsosurfaceMesher.*`, `SceneOrbitalGeometry.*`, orbital N panel, orbital IO.

Touch only what this task needs and list every file you change. Read the reports of tasks 73, 74
and 77. Screenshots attached, in this order: `r4-38.png`, `r4-40.png`, `r4-41.png`.

## 1. "Promień okręgu" must move the arrow ends (screenshots 2 and 3)

User: "Promień okręgu does not move the arrow ends, and it should."
- Task 77 replaced the radial end offset by a bulge scale, because the old trim had a
  discontinuity. Screenshots 2 and 3 show radiusScale 1.356: the arcs bulge but the ends stay at
  the atoms.
- Wanted: a real circle model, continuous in every parameter.
  - Each arc lies on (or, with curvature, between ends on) a circle about the rotation axis of
    radius `R = radiusScale x rho`, where rho is the atoms' distance from the axis (per atom, as
    now).
  - Each end sits on that circle at the atom's angle, moved along the circle by the smallest
    angle delta that keeps the end at least `r_atom x (1 + endGap)` from the atom centre.
    delta = 0 once the circle is already that far from the atom. This is continuous and
    monotonic, with no branch jump.
  - Ends stay bound to the atoms (CopyPosition with an offset, or whatever binding keeps them
    following the atoms) so moving an atom moves the arrow.
  - radiusScale 1 must reproduce today's default geometry (or very close; report the
    difference).
- Keep task 77's continuity sweep tests and make them pass with the new model; add sweeps for
  the ends.

## 2. The user's C_2 sketch must be reachable (screenshot 1)

Screenshot 1:
- Red: what the app makes for two atoms in bond mode, a ring about the bond.
- Yellow: what the user wants to be able to get. A C_2 pair about an axis PERPENDICULAR to the
  bond, through its midpoint: one arrow from the top of the left atom over to the right atom,
  one from the bottom of the right atom back under to the left atom. The ends sit outside the
  atoms, on a circle larger than half the bond.
- With item 1 this is the two-end (non-bond) pair with radiusScale > 1. Make it reachable and
  easy:
  - add an axis choice "Prostopadła do wiązania" for two atoms (C_2 axis through the bond
    midpoint, perpendicular to the bond);
  - use `tiltDegrees` (task 77) to choose the plane the pair lies in;
  - default that plane to the view-independent choice you find most natural (e.g. containing the
    defect z, else the plane of the neighbours), and report it.
- The bond ring (red) stays available as "Oś wiązania".

## 3. Ctrl+Z / Ctrl+Shift+Z do nothing while the redo panel has focus

User: "Ctrl+Z doesn't work in this new window." `OperatorRedoPanel` (Blender's "adjust last
operation") takes ImGui focus and the viewport/global undo shortcut is not routed. Make Ctrl+Z,
Ctrl+Shift+Z and Ctrl+Y work while the mouse or focus is on the panel. In Blender undo then also
closes the panel (the operation is undone); reuse `PollInvalidation`. Find where the keymap gates
shortcuts on viewport hover/focus and treat the redo panel as part of the viewport. Do not add a
second undo path.

## Tests

- Continuity sweeps (radiusScale, endGap, curvature, tilt) incl. the ends.
- radiusScale > 1 moves the ends outward along the circle; ends keep >= r(1 + endGap) clearance.
- The perpendicular-axis pair with tilt 0/90 has the expected plane.
- Undo shortcut routing: a pure test of the gating function if there is one; otherwise describe
  the manual check.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`,
`src/Domain/**`, `src/Core/Undo/**`, nor the task 79/81 files. Polish UI strings with diacritics.
Report in `docs/work/project/tasks/80-arrow-circle-ends-and-redo-undo-report.md`.
