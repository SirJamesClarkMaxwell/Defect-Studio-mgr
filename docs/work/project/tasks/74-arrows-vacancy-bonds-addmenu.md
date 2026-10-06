# Task 74: vacancy bond ends, arrow clearance, two-arrow C_2, Add menu cleanup

Branch `task/70-operator-redo-panel`. Context: tasks 70-73 in `docs/work/project/tasks/`, spec
`docs/superpowers/specs/2026-10-05-operator-redo-panel-design.md`. Three screenshots attached in
this order: (1) vacancy bonds, (2) C_3 cycle, (3) C_2 ring.

## 1. Bonds to a vacancy must end like ordinary bonds (screenshot 1)

User, 4th report: "still a problem with bonds to the vacancy". The vacancy marker is a flat,
camera-facing translucent disc (`OpenGlVacancyRenderer.cpp`, `BuildVacancyMarkerMesh`), drawn
after the paths (`OpenGlRendererBackend.cpp:808,812`). The atom-vacancy line (`MakeVacancyBond`
in `src/Presentation/Panels/ViewportVacancyAdd.cpp`) is cut at a 3D sphere around the vacancy
(`CopyVacancy{.., 1.0f}` buffer). Seen from an angle this gives:
- dark flat end caps visible at the disc edge (C1, C2);
- lines that point toward the camera running into the disc (C3);
- lines visibly thinner than the structure's own bonds. `MakeBondLine` takes
  `bonds.front().radius` and ignores the bond radius multiplier (its own ponytail comment).

Wanted: from every view angle the line disappears at the marker's outline, exactly like a bond
disappearing into an atom sphere. It must be as thick as the real bonds.

Suggested approach (you may choose better):
- end the line at the vacancy centre (buffer 0) for vacancy ends;
- make the marker occlude it like a sphere. For example, the fill writes the depth of a sphere
  of the marker radius (impostor: `gl_FragDepth` from the sphere's front surface), and lines are
  drawn before it, so the part inside the sphere is hidden (opaque/Solid) or tinted through the
  fill (translucent).

Also:
- the atom end keeps its current look;
- vacancy-vacancy lines (V_B-V_N) get the same treatment;
- existing saved lines (buffer 1.0) still look right or are fixed by "regenerate";
- thickness: use the bond radius the bond renderer actually draws (including the global
  multiplier).

No exceptions in render paths. Keep the marker look (ring, fill colour, opacity) otherwise
unchanged.

## 2. Arrow clearance from atoms: adjustable radius and end gap (screenshot 2)

User: "no way to adjust where the arrows start and end; they are quite close to the atom edge;
changing R of the whole circle could help". Add two panel parameters to `CurvedArrowParameters`
and the operator schema (`CurvedArrowOperator.cpp`), relevant for the cycle and the non-bond
two-end arc:
- `radiusScale` "Promień okręgu" (Float, [0.8, 2.5], default 1.0): the arcs lie on a circle
  `radiusScale` x the atoms' distance from the axis. The ends move radially outward from each atom
  and stay bound to the atoms, e.g. via the `CopyPosition` offset. At 1.0 the result is exactly
  today's.
- `endGap` "Odstęp od atomów" (Float, in atom radii, [0, 3], default = today's
  `GetScenePathAtomBuffer()` value): visible clearance between each end (tail and arrowhead tip)
  and the atom sphere surface, independent of `radiusScale`.

Both must change all arrows of a cycle live through the redo panel (task 73 test pattern).

## 3. C_2 needs two arrows (screenshot 3)

User: "for C_2 the second arrow is missing". C_2 is a half turn: the ring about the bond (bond
mode) must show n = 2 arrows, e -> C_2 -> e. Add `arrowCount` "Liczba strzałek" (Int, [1, 6],
default 2), relevant in bond mode only:
- the ring is split into `arrowCount` equal arcs;
- `sweepDegrees` becomes the sweep of ONE arc, clamped to `360/arrowCount - 5`, with a new
  default of 150 for the default count of 2;
- `rotationDegrees` rotates the whole set;
- all arcs keep the `BondFrame` binding, are one undo step and are all selected.

The bond-mode radius already exists (`radiusFactor`). Check that the default keeps the ring
visibly clear of both spheres for diamond C-C. If not, change the default and say so. Update the
redo-panel relevance rule (bond mode: + arrowCount; cycle/two-end: + radiusScale, endGap).

## 4. Remove dev path items from the Add menu

User: "why do we still have dev curve stuff? We finished that editing - clean Shift+A and
RMB > Add". `DrawSceneAddMenu` (`ViewportAddMenu.cpp:139`) still calls `DrawScenePathDevAddMenu`
(`ScenePathDevMenu.cpp:190`), which offers "Path > Dev > Decoration gallery / Cubic S-curve /
Mixed segments / Thick curved Flat ribbon" and "Tube 3D / Flat ribbon / Camera-facing ribbon >
Line / Cubic / Arc".
- Remove that call from the user Add menu.
- Keep ONE plain submenu "Krzywa" with "Prosta", "Krzywa Béziera" and "Łuk". It creates the round
  3D tube, which is the default; the profile is changed in the N panel.
- Do not duplicate items that `DrawFreeSegmentAddItems` already has.
- Keep `AddScenePathDecorationGallery` (a test uses it); delete code that becomes unused.
- While there: fix the missing Polish diacritics in `RendererPanelOrbitalMenu.cpp` ("Strzalka
  swobodna", "Plaszczyzna swobodna", and any other ASCII-only Polish labels in the Add menu files).

## Tests

- Unit tests for 2 and 3: `radiusScale` 1.0 reproduces today's geometry; >1 moves the arc midpoint
  outward; `endGap` changes the resolved end distance from the sphere surface; bond mode with
  `arrowCount` 2 gives 2 paths with sweep <= 175 degrees, evenly spaced; the redo panel changes
  all of them.
- For 1: test the pure part (line thickness from the effective bond radius; vacancy ends at the
  centre). The shader part is checked visually by the caller.
- Do not weaken existing expectations; if a default changes, update only that expectation and
  say so.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`,
`src/Domain/**`, `src/Core/Undo/**`. Polish UI strings with diacritics. New source files need a
premake regenerate by the caller; list them. Report in
`docs/work/project/tasks/74-arrows-vacancy-bonds-addmenu-report.md`: files changed, tests,
decisions, defaults chosen.
