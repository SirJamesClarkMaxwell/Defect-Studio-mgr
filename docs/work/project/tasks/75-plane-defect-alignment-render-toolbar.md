# Task 75: planes in defect axes, plane transparency, vertical toolbar

Branch `task/70-operator-redo-panel`. Runs after task 74 (same tree; 74 touched
`ViewportAddMenu.cpp`, the vacancy renderer and the curved-arrow files). Read its report
`docs/work/project/tasks/74-arrows-vacancy-bonds-addmenu-report.md` first and do not undo it.

User: "do we have 'align plane to'? It should be easy to align a plane to the defect and the
defect axes. Is the plane the same 3D object as everything else? Is the vertical toolbar
complete? Check the plane render through atoms - do we really see what we should?"

## What exists (checked by the caller)

- Orientation only:
  - "Osie defektu > Wyrównaj zaznaczone do osi": local axes = defect axes, normal = z
    (`ViewportDefectFrame.cpp:462`);
  - "Wyrównaj oś obiektu..." with 9 own/defect axis pairs (`SceneOrientationControls.cpp:104`);
  - the same as two combos in the plane N panel.
- Nothing moves a plane onto the defect: the centre stays where it was.
- Planes are full scene objects for: selection, G/R/S, Ctrl+D/C/V, delete, outliner eye, save,
  parenting to the defect axes and atom anchoring. But:
  - they store centre/normal/tangent, and the N panel shows a raw "Normalna" vector, with no
    rotation field like other objects;
  - the plane UI strings have no Polish diacritics ("Plaszczyzny", "Srodek", "Polowa rozmiaru",
    "Przezroczystosc", "Wspolne pola ponizej...", "Os obiektu", "Os defektu", ...).
- Render: the plane fill goes through `renderIsosurfaceGpuOverlay`. In the non-outline pass it
  leaves depth writes ON (`OpenGlRendererBackend.cpp:~2795`), and planes are drawn BEFORE
  vacancy markers, scene orbitals and the orbital isosurfaces (`OpenGlRendererBackend.cpp:809-826`).
  Anything drawn later that lies behind a plane is depth-rejected and disappears instead of being
  seen through the translucent plane. Atoms and bonds behind the plane are tinted correctly, and
  atoms cut by the plane look right.
- Vertical toolbar (`ViewportVerticalToolbar.cpp`):
  - every `SelectionToolMode` and G/R/S has a button;
  - the three add buttons (arrow, plane, orbital, lines ~284-332) all open the SAME full
    `DrawSceneAddMenu`;
  - tooltips are English while the UI is Polish.

## Do

1. **Plane in defect axes, one click, one undo step:**
   - In "Osie defektu" and in the plane N panel (single and multi selection): "Ustaw w osiach
     defektu" with three choices: "płaszczyzna xy (⟂ z)", "płaszczyzna xz", "płaszczyzna yz".
     Each sets the normal and the tangent from the defect frame AND moves the centre to the
     defect origin.
   - A separate "Przenieś środek na środek defektu" (position only, keeps orientation), usable
     for orbitals and paths too, wherever the object has an origin.
   - Anchored planes: say in the tooltip that this detaches them ("Odczep"), or disable it for
     them. Your choice; report it.
   - The Add menu plane items get the same three presets when a defect frame exists: a new plane
     in the defect axes at the defect origin, sized like today's default.
2. **Plane transparency:**
   - Draw planes after vacancy markers, scene orbitals and orbital isosurfaces.
   - Draw them with depth writes OFF, sorted back to front by centre distance, so everything
     behind a plane stays visible through it, tinted, from every view.
   - Keep the selection outline readable.
   - Make sure the overlay helper restores the depth mask for its other users. Add a parameter
     or a separate call; do not change how isosurfaces/orbitals look.
   - No exceptions in render paths.
3. **Plane N panel:**
   - Add an orientation field like the other objects' rotation, e.g. Euler XYZ degrees derived
     from the normal/tangent frame and written back to it.
   - Keep "Normalna" as an advanced/readonly line or remove it; report which.
   - Fix all the plane UI diacritics listed above (and in the combos of
     `SceneOrientationControls.cpp`).
4. **Vertical toolbar:**
   - Each add button opens only its own part of the Add menu: arrow = "Rysuj" items, plane =
     plane items (incl. the presets), orbital = orbital items.
   - Factor sections out of `DrawSceneAddMenu` so RMB/Shift+A still show the full menu built
     from the same sections. No duplicate item lists.
   - Tooltips in Polish with shortcuts.
   - Do not add new tools.

## Tests

Unit tests (pure functions in `Renderer/Scene` or `Presentation`):
- the xy/xz/yz presets give the expected normal/tangent for a rotated defect frame and put the
  centre on the origin;
- Euler <-> (normal, tangent) round trip;
- back-to-front plane ordering.

Rendering is checked visually by the caller.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`,
`src/Domain/**`, `src/Core/Undo/**`. Polish UI strings with diacritics. List new source files
(premake regenerate). Report in
`docs/work/project/tasks/75-plane-defect-alignment-render-toolbar-report.md`.
