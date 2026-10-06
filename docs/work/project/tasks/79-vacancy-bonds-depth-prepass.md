# Task 79: vacancy bonds - depth pre-pass so the line really ends at the marker

Branch `task/70-operator-redo-panel` (HEAD `5eb406e`). Two other codex agents work in the same
tree at the same time:
- task 80: curved arrows + redo panel; `ScenePathCurvedArrow.*`, `src/Presentation/Operators/**`,
  `OperatorRedoPanel.*`, `CurvedArrowParameters.hpp`;
- task 81: orbital smoothness; `IsosurfaceMesher.*`, `SceneOrbitalGeometry.*`, orbital N panel,
  orbital IO.

Touch only what this task needs and list every file you change. Read the reports of tasks 74
and 76 (`docs/work/project/tasks/`). Screenshot attached: `r4-39.png`, after the user
regenerated the bonds with task 76's code.

## Why task 76 did not work (caller's diagnosis; verify it)

- Generated lines now run to the vacancy centre without caps. The marker fill writes analytic
  sphere depth, but the marker is drawn AFTER the paths (`OpenGlRendererBackend.cpp`: paths
  ~808, vacancy markers ~812).
- So the line is already in the colour buffer and the translucent fill only tints it. A black
  line under a dark-grey translucent fill looks unoccluded: in the screenshot every line visibly
  runs to the centre and over the disc.

## Wanted (unchanged)

From any angle, perspective and orthographic, the line must look like a bond entering an opaque
ball: it disappears exactly where it passes behind the marker's front sphere surface, i.e. at the
marker outline. No tinted line inside the disc, no flat cap. Parts of a line that are really in
front of the sphere (outside it, nearer the camera) stay visible. The background must still show
through the translucent fill as today.

## Do

1. **Depth pre-pass:**
   - before the depth-tested path pass, draw every visible vacancy marker as depth-only:
     colour mask off, writing the same analytic front-sphere depth the fill uses, for the disc
     out to its outline radius;
   - then draw the paths (their parts behind the sphere front fail the depth test);
   - then draw the marker colour pass as today (LEQUAL against its own depth).
   - Make sure atoms and bonds drawn earlier are not damaged: depth-only writes nearer depth
     only where the marker is in front.
   - Make sure objects that should stay visible through a translucent vacancy are not
     swallowed. Decide and report:
     - atoms/bonds behind the vacancy are drawn before and keep their colour;
     - orbitals/planes drawn later behind the marker would now be hidden. Acceptable? Only
       if they were already hidden by the fill's depth write in task 74/76; otherwise keep
       them visible, e.g. by restricting the pre-pass to the path pass.
2. **Atom end:** match `renderBonds` (`OpenGlRendererBackend.cpp:~1564-1580`). Ordinary bonds
   stop at `sqrt(r_atom^2 - r_tube^2)` from the atom centre (rendered tube radius incl. the
   multiplier), so their rim touches the sphere surface exactly.
   - The screenshot's top N atom shows the vacancy line's tube inside the sphere showing through
     (atom depth may not be an exact sphere). Do the same for the vacancy line's atom end:
     CopyPosition buffer = `sqrt(1 - (w/r)^2)` atom radii, computed at creation from the atom's
     radius and the line's tube radius.
   - Keep it consistent when the path width changes later, if cheap. If not, report.
3. **Regeneration** still replaces old generated bonds: the binding/style matcher from task 76
   must accept the new atom buffer too.

## Tests

- Pure tests: atom-end buffer formula; matcher accepts the new buffer; replacement unchanged.
- Rendering: describe the exact visual checks for the caller (angles, solid/translucent,
  orthographic, a vacancy in front of / behind an orbital and a plane).

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`,
`src/Domain/**`, `src/Core/Undo/**`, nor the task 80/81 files above. Keep orbital/isosurface
shader behaviour identical. No exceptions in render paths. Report in
`docs/work/project/tasks/79-vacancy-bonds-depth-prepass-report.md`.
