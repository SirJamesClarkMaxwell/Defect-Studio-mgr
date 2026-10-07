# Backlog

Open items found during manual testing that are not yet scheduled into a task.

- **`scripts/python/build.py` exits 0 on a failed build** (2026-10-07, task/84). MSBuild prints
  "Kompilacja NIE POWIODLA SIE / Liczba bledow: 10" and the wrapper still reports
  `[exited with code 0]`, leaving the previous `.exe` in place. Any check that trusts the exit
  code - a script, a hook, an agent - silently tests a stale binary. The wrapper has to
  propagate MSBuild's exit code.

- **Undo after a small atom move looks slightly off** (2026-09-13, manual run of the hide/delete fix). Hard to
  describe; it worked when repeated. Reproduce with Object Properties open (coordinates visible): move an atom
  a tiny amount with the gizmo / G, then Ctrl+Z, and compare coordinates before/after. Suspects: float
  round-trip of Cartesian vs fractional positions in the transform command's undo, or a gizmo drag
  producing several commands.
- **Flaky `JobSystemControlTests.RetryResubmitsFinishedJob`** (2026-09-13, full Release run on
  task/23-group-theory-panel). Failed once in the full suite at `JobSystemControlTests.cpp:84` (got 1, expected 0),
  passed 5/5 when run alone. Timing-dependent - likely waits on job state without a condition wait; look for a
  sleep/poll race with other tests' load.
- **Python example scripts are found relative to the working directory, not the executable**
  (`ResolvePythonExampleScript`, `ScientificRuntime/Python/ScriptBridgeUtils.cpp`). An exe built in one
  checkout but started from another repo's directory silently uses that repo's scripts ("Python script
  file was not found" for scripts that only exist in the exe's own checkout). Also search upward from the
  executable's directory. *Fixed on task/23-group-theory-panel (executable directory searched first).*
- **Ctrl+S refused every edited structure in a data root** (2026-10-03, manual run of task 54).
  "New project" puts the manifest in its own subfolder and registers the folder with the structures as a data
  root. `SaveProjectWithSceneObjects` only counted `projectDirectory` as "inside the project", so every edited
  POSCAR got `project_save.structure_outside_project`. *Fixed on task/59-manual-test-fixes
  (`manifest.roots` count as project directories; test `EditedStructureInDataRootIsWritten`).*
- **"Save File" (`file.save`, Ctrl+Alt+S) does nothing** (2026-10-03). `EditorLayer::onStructureFileSaveRequested`
  has been empty since Step 11, but the command is still registered with a "structure or text document"
  description. Either wire it to save the focused structure (the same writer as Ctrl+S) or unregister it.
- **Project manifests keep absolute root paths** (2026-10-03). After the repo moved from
  `C:\Users\fzabi\Desktop\STUDIA\...` to `D:\STUDIA\Fizyka\...`, every older manifest's roots are dead ("ignoring
  project root ... cannot find the path"). Store roots under the project directory as relative paths, or offer to
  relocate a missing root on open.
- **Deleting an orbital deletes the selected atoms** (2026-10-03). Clicking an orbital, plane or path does not
  clear the atom selection (an arrow click does, `ViewportSceneArrowInteraction.cpp:252`). "Wiązania zwisające →
  wakans" also leaves the atoms selected. Del in the viewport always runs `renderer.selection.delete`, which
  deletes atoms. Scene objects can only be deleted from the Outliner. Fix: clear the structure selection on a
  non-Ctrl click, and add a `renderer.scene_objects.delete` bound to Delete under a "scene object selected" context
  (the `path_edit.delete_nodes` pattern). *Fixed on task/59-manual-test-fixes: a plain orbital/plane/path click and
  every Add > Orbital clear the atom selection (Delete already removed selected orbitals through
  `HandlePinnedMeasurementKeyboardShortcuts`; it also hit the leftover atoms through the keymap). Ctrl+click still
  mixes atoms and objects on purpose.*
- **Multi-selection of orbitals** (2026-10-03). Ctrl+click is implemented
  (`ViewportSceneOrbitalInteraction.cpp:60`) but was reported as not working - check in the app. Box/circle select
  only picks atoms. *task/59-manual-test-fixes: box/circle select now also take orbitals (bounding-sphere centre)
  and vacancy markers.*
- **Vacancies cannot be selected or styled one by one** (2026-10-03). The only style is global (Element Catalog >
  Vacancy). There is no viewport pick, no Object Properties section and no per-vacancy style. This goes with task
  55 (vacancy semantics) - vacancies become scene objects. *task/59-manual-test-fixes: click/Ctrl+click/box select a
  marker, highlight ring, Object Properties section (position, label, delete, shared style editor), Delete key.
  Per-vacancy colour added 2026-10-04 (`VacancySite::color`, saved in scene_objects.yaml); size/mode stay shared.*
- **Line/cylinder between a vacancy and an atom** (2026-10-03). Not a domain bond (a vacancy is not an atom): a
  `SceneArrow` Line 3D with a vacancy anchor next to `start/endAnchorAtom`, plus a "Połącz zaznaczone atomy z
  wakansem" menu item. Same task as vacancy selection. *Reported working by the user on 2026-10-03.*
- **User-chosen orbital basis for SALCs** (2026-10-03). The Group Theory panel offers one function for every site
  (sp³ / p → centre / s). An arbitrary per-site basis (p_x, p_y, d ...) needs D(g) = permutation ⊗ orbital
  rotation in the Python reduction, not just permutations. Plan it as its own task after 57.
- **Build setup on the D: machine** (2026-10-03). MSBuild is at
  `D:\Aplications\VisualStudio\VisualStudioIDE\MSBuild\Current\Bin\MSBuild.exe` and only toolset v143 is installed,
  so projects must be generated with `DS_TOOLSET=msc-v143`. `CLAUDE.md` and the `full-build-verify` skill still
  name the `C:\Program Files\...\18\...` path - switch them to `vswhere`.
- **Atoms could not be clicked in the "All" selection mode** (2026-10-03). Orbitals and planes picked with their
  whole bounding volume before atoms, so the atom an orbital sits on was unclickable. *Fixed on
  task/59-manual-test-fixes: an atom on the ray wins (`PickAtomAlongRay`).* ponytail: orbitals still pick by
  bounding sphere, so empty space near a lobe selects the orbital; a mesh pick is the upgrade.
- **Invert selection had no menu entry** (2026-10-03). It existed only as the I key. *Context menu item added.*
- **Defect axes** (2026-10-03, task/59-manual-test-fixes). `CrystalStructure::defectFrame` (origin + right-handed
  x/y/z), set from the viewport menu "Osie defektu" (vacancy -> selection, atom 1 -> atom 2, cursor ->
  selection, x toward selection, flip, remove; undoable `renderer.defect_frame.set`), saved in scene_objects.yaml,
  drawn as an x/y/z "empty" toggled in the eye menu. While shown, 1/2/3 look along x/y/z and Shift+1/2/3 along
  a/b/c. The Group Theory panel sends the sites in these axes and the script tries the identity / same-z frames
  first, so E rows refer to the defect z. Orbitals get "Ustaw w osiach defektu". Open: the frame does not follow
  a relaxation (positions, not atom bindings - task 56's binding would cover it); the triad is not in PNG export.
- **Labels were hard to click** (2026-10-04). The pick was a 16 px circle round the label anchor, but the label is
  drawn with the bond auto-offset, rotation, scale and padding, so the visible box was mostly outside it. *Fixed on
  task/59-manual-test-fixes: the renderer records each pinned/free label's drawn rect (`LabelPickQuads`) and the
  click tests that; the old radius stays as fallback for labels without one (all-bonds mode).*
- **Hidden atoms partly came back after reopening a diamond project** (2026-10-04). Hidden atoms are stored by
  position; a site saved at x = -1e-7 is reloaded wrapped to x = a, a whole cell away. *Fixed: position lookup
  uses the minimum image when the structure is periodic (`RendererStructureData::periodic`).*
- **Project Tree "New Folder" looked broken** (2026-10-04). The folder was created inside a collapsed parent, so
  nothing appeared; the second try then failed with "Failed to create" because it existed. New files never
  refreshed the listing at all. *Fixed: listing refreshed for both, parent expanded, new entry selected, "already
  exists" said plainly; with nothing selected and one root the buttons create in that root.*
- **Vacancies and defect axes could not be moved** (2026-10-04). *Fixed: both are G/R/S + gizmo targets (preview on
  the renderer copy, commit through the undoable set commands with an `edit` on the domain value). The axes are
  selectable (click the origin or an axis, Scene Outliner row with the visibility eye, Object Properties: origin,
  flip, remove, Delete); a vacancy's position can also be typed in Object Properties.*
- **Transforms in the defect axes** (2026-10-04). *Added: transform orientation "Defect axes" (toolbar, gizmo,
  navigation gizmo, G/R/S constraints); for atoms X X falls back to the defect axes when there is no Local frame.
  With the axes in the selection the pivot is their origin and Local = the axes.*
- **Align objects to the defect axes / temporary parenting** (2026-10-04). *Added to the right-click "Osie defektu"
  menu: align the selected arrows/lines/planes/orbitals/paths to the axes (one undo step), "Przypnij zaznaczone"
  (G/R/S of the axes carries free labels, arrows, orbitals, planes and vacancies; per session, not saved), unpin.
  Ad hoc: select objects, Ctrl+click the axes. ponytail: paths cannot be pinned; pinned vacancy indices shift when
  an earlier vacancy is deleted.*
- **Add menus** (2026-10-04). *Vacancy, "Vacancy bonds" and the "Defect axes (empty)" submenu (here with world
  axes, vacancy -> selection, atom 1 -> atom 2, cursor -> selection, from the selected object's axes) are in both
  Shift+A and right-click Add; the right-click "Osie defektu" menu only edits existing axes.*
- **Two-colour atom-vacancy bonds** (2026-10-04). *Added: Add > "Vacancy bonds" and the vacancy's "Wiązania do
  sąsiadów" button make Line scene objects anchored to the atom, gradient atom colour -> vacancy colour, bond
  thickness; each is styled on its own. ponytail: the vacancy end does not follow a later vacancy move.*
- **Bond label gizmo stood off the label** (2026-10-04). The gizmo sat on the bond midpoint + manual offset, the
  label is drawn further out by the auto-offset. *Fixed: the anchor is the label's drawn centre (LabelPickQuads).*
- **Bond label options in the N panel** (2026-10-04). *Selected labels section now has Placement: align to bond,
  flip, offset (live, one undo step), rotation, and the view-wide auto-offset / align threshold.*
- **Defect axes gizmo used world axes** (2026-10-04). *Fixed: while the axes are transformed, Global means their own
  axes (`SceneTransformOrientation`; X X gives Global). Re-aim entries (z / x toward the selection, world axes,
  cell axes z = c) in the context menu and in Object Properties.*
- **Vacancies from deleted atoms had the generic colour** (2026-10-04). *Fixed: `VacancyRenderStyle::colorBySpecies`
  (default on, `color_by_species` in the style file) gives a vacancy the removed element's colour.*
- **Selected labels drew boxes** (2026-10-04). The glyph selection outline was wider than the MSDF distance reach,
  so each glyph quad filled. *Fixed in labels.frag (capped outline + tint); a label background gets a
  selection-coloured frame.*
- **Idle memory growth** (2026-10-04). The app's private bytes grow ~1.3-2 MB/s with no input, also with no
  structure window open; an instance left running for ~2 h reached 10.7 GB. Present already at 27ee4c6 (before
  tasks 59-64), handle and thread counts stay flat, so it is heap or driver memory grown every frame. Not
  `EventQueue` (drained). *Fixed: Tracy was built with TRACY_ENABLE but without TRACY_ON_DEMAND, so the client
  queued every zone from startup in its own allocator (outside the CRT heap - a CRT heap diff showed only
  ~1 KB/frame and the renderer issued no GL allocations with no window open). With TRACY_ON_DEMAND private bytes
  stay flat (392 MB over a minute idle).*
