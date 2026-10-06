# Task 76: bonds to a vacancy - make them look like real bonds, for real

Branch `task/70-operator-redo-panel` (HEAD `b435586`). Two other codex agents work in the same
tree at the same time (task 77: curved arrows; task 78: selection/hide/orbitals/orientation).
Touch only what this task needs and list every file you change. Screenshot attached (the user's
5th report on this): `t76-vacbond.png`.

## History

Tasks 69 and 74 already tried. Read
`docs/work/project/tasks/74-arrows-vacancy-bonds-addmenu-report.md` first.

- **Task 74:** vacancy ends at the centre; the marker fill writes analytic sphere depth
  (`src/Renderer/OpenGl/Shaders/isosurface.frag`, `OpenGlVacancyRenderer.cpp`); width from the
  effective bond radius.
- **Caller:** removed a name-based legacy hack (`path.name == "Vacancy bond"` in
  `PathBindingResolver.cpp`). Old saved lines (CopyVacancy buffer 1.0) therefore still end at a
  sphere with a flat cap. The user's project may still contain them.

## What the screenshot shows (diamond NV-like site, vacancy marker with defect axes)

1. **N atom (top):** a light ellipse, the flat end cap of the vacancy line, pokes OUT through the
   N sphere. The atom end starts at 0.9 atom radius (`kAtomEndBuffer` in
   `ViewportVacancyAdd.cpp`). With the now thicker tube, the cap rim lies outside the sphere
   (cap rim distance sqrt((0.9r)^2 + w^2) > r).
2. **C1 and C2 lines:** end at the marker outline with a dark flat cap. This is probably an old
   buffered line; verify that a NEW line cannot look like this.
3. **Bottom (C3) line:** drawn over the lower part of the marker disc.

## Wanted

Exactly like an ordinary bond between two atoms, seen from ANY angle, perspective and
orthographic:
- the tube enters the atom sphere and the sphere hides the end;
- the tube enters the vacancy marker and disappears at the marker outline. Solid marker: fully
  hidden inside. Translucent marker: visible only as tinted through the fill, never on top of it,
  and never with a visible flat cap;
- same thickness as ordinary bonds.

## Do

1. **Atom end:** start at the atom centre (buffer 0), like the bond renderer does, so the sphere
   hides it. Check how ordinary bonds are drawn (`renderBonds`) and match them, including the
   two-colour split if bonds use one.
2. **Prove the vacancy-side occlusion works for new lines:**
   - find every reason a new line could still show a cap or draw over the disc: draw order, the
     path pass drawn twice (`OpenGlRendererBackend.cpp:808` and `:836`, the second "overlay"
     pass), depth func, the fill depth radius vs. the visible disc radius, the dashed ring,
     Wireframe mode, the camera ray in orthographic views;
   - fix what you find. If the second path pass ignores depth for selected paths, the vacancy
     line must not use it unless selected.
3. **Old lines:** "Wiązania do sąsiadów" (and the other vacancy-bond generators) must REPLACE
   the existing generated vacancy bonds for the same atom-vacancy / vacancy-vacancy pairs
   instead of adding duplicates. One click then fixes an old project.
   - Identify a generated bond by its bindings (CopyPosition + CopyVacancy, or two CopyVacancy,
     with the generated style), NOT by its name.
   - One undo step.
   - Also say in the report how the user upgrades an old project.
4. **Tube pokes through:** if any other path type (straight Arrow / Line through atoms, curved
   arrows) has the same problem, list it in the report; do not change it.

## Tests

- Pure tests: the atom end at the centre; regeneration replaces instead of duplicating, with one
  undo step, and leaves non-generated user paths with vacancy ends alone.
- A depth test of the analytic sphere function if it is a pure helper.
- Describe precisely what the caller must check visually: angles, solid vs. translucent, ortho.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`,
`src/Domain/**`, `src/Core/Undo/**`, `src/Presentation/Panels/ScenePathCurvedArrow.*`,
`src/Presentation/Operators/**`, `src/Renderer/Path/CurvedArrowParameters.hpp` (task 77),
`src/Renderer/RendererLayer.cpp`, the orbital renderer/mesh files and selection code (task 78).
If you must change `isosurface.frag`, keep the orbital/isosurface path bit-identical in behaviour.
No exceptions in render paths. Report in `docs/work/project/tasks/76-vacancy-bonds-for-real-report.md`.
