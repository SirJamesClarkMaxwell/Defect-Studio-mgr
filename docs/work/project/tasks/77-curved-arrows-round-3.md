# Task 77: curved arrows - radius jump, two arrows for two ends, tilt, selectable 3D objects

Branch `task/70-operator-redo-panel` (HEAD `b435586`). Two other codex agents work in the same
tree at the same time:
- task 76: vacancy bonds; `ViewportVacancyAdd.*`, vacancy renderer, `isosurface.frag`;
- task 78: selection / hide / orbitals / defect-axis orientation; `RendererLayer.cpp`, orbital
  renderer, `ViewportSelection*`, `ViewportInteraction*`.

Touch only what this task needs and list every file you change. Read the reports of tasks 73 and
74 in `docs/work/project/tasks/`.

Screenshots attached, in this order:
- `t77-before.png` and `t77-jump.png`: a C_3 cycle before and after a small change in the redo
  panel;
- `t77-c2.png`: two atoms in hBN with "Oś Z defektu".

## 1. Big jump (screenshots 1 and 2)

User: "here there is a very big jump". A small change of a redo-panel value turns the gentle C_3
arcs (screenshot 1) into a huge Reuleaux-like triangle far outside the atoms (screenshot 2).
Most likely "Promień okręgu" (`radiusScale`) and/or "Odstęp od atomów" (`endGap`): the radial
CopyPosition offset, the clearance trimming, and the arc fit to the trimmed chord with
`curvature` x angle.

Find the discontinuity: write a test sweeping each parameter in small steps (e.g. radiusScale
1.00..1.50 by 0.01, endGap 0..1 by 0.01, curvature 0.05..1.5) and asserting that the resolved
geometry changes continuously (bounded change of the arc midpoint per step). Then fix the cause.
Expected behaviour of radiusScale:
- the arcs move outward smoothly;
- at 1.3 they still look like screenshot 1, only on a bigger circle;
- the arrow ends stay near their atoms (outside them by endGap).

## 2. Two ends need two arrows, and a tilt (screenshot 3)

User: "still missing the 2nd arrow + again no way to rotate this arrow so it is perpendicular to
the whole crystal plane".

- For TWO ends in non-bond mode (axis "Oś Z defektu", or atom/vacancy pairs), make
  `arrowCount` relevant too: 1 or 2, default 2. With 2: A -> B and B -> A, bulging to opposite
  sides of the chord (a C_2 pair), one undo step, all selected.
- Add `tiltDegrees` "Nachylenie" (Float, -180..180, default 0) for the cycle and the two-end
  arcs. It rotates each arc's plane about its own chord (the line between its two ends), so the
  arcs can stand perpendicular to the crystal plane (90 deg). Ends stay bound to the atoms.
- In bond mode the existing `rotationDegrees` stays.
- Update the relevance rule and the schema. Check that the panel shows the right set in every
  mode.

## 3. Arrows must be ordinary selectable 3D objects

User: "these added arrows don't seem to be normal 3D objects and they can't be selected".

- Find why a click on a curved arrow does not select it. Suspects:
  - the 0.03 A stroke is under the pick tolerance;
  - arcs with `BondFrame` / bound ends resolved differently in picking than in rendering;
  - Free-binding bond-mode arcs in a transformed frame.
- Path picking lives in `src/Renderer/Path/ScenePathPicking.*` and `PathPicking.*`. Fix it so a
  click within a few pixels of the visible stroke selects the arrow in the default selection mode
  and in "Wszystko" (Ctrl+4). Box/circle select must include them too.
- "Not 3D-looking": created curved arrows get `style.shadeSmooth = true` (the round tube is
  flat-shaded today). Report if the head also needs it.

## Tests

- The continuity sweep from 1.
- Two-end `arrowCount` 2: geometry (opposite bulge), bindings, undo.
- `tiltDegrees`: 90 puts the arc midpoint off the original plane by the arc height; ends unchanged.
- Picking: a pure test that a ray passing 2 px from a 0.03 A arc at a typical zoom hits it; bound
  and BondFrame arcs pick where they render.
- Keep the existing expectations; update only what this task deliberately changes, and say so.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`,
`src/Domain/**`, `src/Core/Undo/**`, nor the task 76/78 files above. Polish UI strings with
diacritics. Report in `docs/work/project/tasks/77-curved-arrows-round-3-report.md`.
