# Task 81: orbitals still look faceted - fix it, add a "Gładkie cieniowanie" checkbox

Branch `task/70-operator-redo-panel` (HEAD `5eb406e`). Two other codex agents work in the same
tree at the same time:
- task 79: vacancy bonds/renderer; `ViewportVacancyAdd.*`, vacancy renderer,
  `OpenGlRendererBackend.cpp`, `isosurface.frag`;
- task 80: curved arrows/redo panel.

Do not touch their files; list every file you change. Read task 78's report (smooth normals via
finite-difference gradients in `IsosurfaceMesher.cpp`). Screenshot attached: `r4-42.png`, scene
orbitals at resolution 46.

## Problem

User: "these orbitals are still not smooth enough, resolution is 46, and I don't see a
shade-smooth checkbox". In the screenshot:
- silhouettes are visibly polygonal/jagged;
- the lit surface shows dark dimples and creases, e.g. a dark spot near the centre of each red
  lobe and a crease on the blue lobes;
- facets are visible in the specular.

## Do

1. **Find why** a resolution-46 marching-tetrahedra mesh with gradient normals still looks like
   this. Candidates:
   - normals not reaching the GPU or overwritten per-face somewhere;
   - the gradient taken on a coarse grid different from the meshing grid, or with a bad
     finite-difference step;
   - normals flipped on some vertices (dark spots);
   - the "resolution" not being the grid that is actually meshed;
   - marching tetrahedra producing slivers that need vertex welding;
   - the stretch/affine normal transform.

   Report the cause with evidence (a unit test that fails before the fix).
2. **Fix it:**
   - welded vertices with averaged normals (shared positions, no duplicate per-face vertices)
     and/or analytic gradient normals from the wavefunction if available;
   - consistent orientation per lobe sign.
   - The silhouette should look round at 46; if needed, raise the default/maximum resolution or
     use a better mesher variant, within the existing code.
3. **"Gładkie cieniowanie" checkbox** in the orbital N panel (single and multi selection, like
   other shared fields): per-object flag, default ON, saved in the scene file (backward
   compatible: missing = on), undoable like other orbital edits. Off = flat face normals, for
   people who want the faceted look.

## Tests

- A sphere-like or hydrogenic test surface at the default resolution:
  - the angle between each vertex normal and the analytic gradient direction stays below a few
    degrees;
  - no vertex normal points inward;
  - welded mesh: shared vertices are not duplicated.
- IO round trip of the flag (old file without it = on).

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`,
`src/Domain/**`, `src/Core/Undo/**`, nor the task 79/80 files. Do not change `isosurface.frag`.
Polish UI strings with diacritics. Report in
`docs/work/project/tasks/81-orbital-smoothness-report.md`.
