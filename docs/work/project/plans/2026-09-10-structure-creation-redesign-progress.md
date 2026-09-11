# Work note: Structure Creation UI Redesign — progress, 2026-09-11

Branch: `task/18-structure-lifecycle-redesign`. Plan being implemented: `PLAN.md` (repo root,
untracked). Review log: `PLAN-REVIEW-LOG.md` (Act 1 locked; Act 2 / Codex review never run).

Every section of the plan is implemented. Release builds clean (`DefectStudio.exe` and
`DefectStudioTests.exe`), **302 passed / 2 skipped** (the 2 skips are the permanent
`DS_PYTHON_CAPI_AVAILABLE=0` ones; baseline was 293 passed, +9 from the new expansion tests).
A 20-second launch produced three pane windows and no errors in `logs/DefectStudio.log`.

Nothing is committed yet. Debug/Dist are deliberately not built (Release-only during active dev).

---

## Done

### 1. Domain — lattice separated from basis

- `BravaisLattice.{hpp,cpp}`: `GetCenteringPresetBasis` renamed to `GetCenteringTranslations`,
  values unchanged, doc comment rewritten (it called lattice points a "basis", which was the
  confusion the plan exists to remove). 4 call sites updated.
- `Domain/Crystal/LatticeBasisExpansion.{hpp,cpp}`: `ExpandBasisOverLattice` (basis-major, wraps
  into `[0,1)`, leaves the Cartesian `position` to the caller) and `FindCoincidentAtomIndices`
  (periodic O(n^2) report, never a dedupe).
- `tests/Domain/Crystal/LatticeBasisExpansionTests.cpp`: 9 tests — P/I/F/C counts, diamond (2 rows
  → 8 atoms), rocksalt species split, wrap-around, basis-major ordering, coincidence detection.
  Note the coincidence case: the centering translations form a group, so a single row never
  collides with itself; two rows differing by a translation do.

### 2-3. Naming and session lifecycle

- `ToString(CreationMode)` now returns `Create New` / `From Library` / `Import File` /
  `Analyze Existing`, matching the tabs.
- `CreationSession` gained `motifStructure`, `leftFraction`, `bottomFraction`, `activePaneIndex`;
  `previewVisible` now means **[0] basis, [1] unit cell, [2] supercell** (Analysis is gone).
- `NewStructureWizardPanel::ensureSession()` extracted and called from `Render()` as soon as one
  basis row has a species — the panes are the draft's viewport, not a reward for hand-off.
  `moveToStructureHub()` is submit-only.

### 4. One window, three panes

- `ViewportToolbars.{hpp,cpp}` + `ViewportVerticalToolbar.cpp`: `drawViewportToolbar` /
  `drawViewportVerticalToolbar` lifted out of `RendererPanel` as free functions taking
  `(RendererWindowState &, RendererLayer &)`. Pure moves plus the `m_Layer` → `layer` rename.
- `ViewportInput.{hpp,cpp}`: same treatment for `applyViewportInputNavigation`, which also lost its
  unused `imageOrigin` parameter.
- `RendererPanel::render()` skips windows with a non-empty `sessionId`; the `dockingInitialized`
  special case for them is gone.
- `StructureCreationTabsPanel` rewritten: no `DockBuilder`, no `m_SessionDockNodes` /
  `m_LaidOutSessions`. One `Begin`, one horizontal + one vertical toolbar acting on the active
  pane, then basis | unit cell on top and supercell full width below, with draggable splitters
  writing into the session. A hidden pane keeps its renderer window — that is what removes the
  `DockNodeUpdateHasCentralNodeChild` crash.

### 5-7. Wizard

- `buildStructure()` convolves `ExpandBasisOverLattice(motifAtoms(), m_Centering)`; `buildMotifStructure()`
  feeds the basis pane.
- Centering is a plain `BravaisCenteringPreset` (no `optional`), drawn as a radio group that never
  touches the basis table. An unsupported preset after a crystal-system switch falls back to P.
- New collapsing **Generated atoms** section, collapsed by default, flagging coincident sites.
- `applyPrototypeToBasis()` factors each site's conventional-cell positions back into
  centering × motif (greedy cover), falling back to P + all positions with a log line when a
  prototype does not factor.
- Four file-load routes: Browse (`Platform::PickOpenFile`), Project Tree selection (forwarded by
  `EditorLayer::onProjectTreeSelectionChanged` to the wizard, whose panel id is now kept), active
  viewport (rebuilt from `RendererStructureData`, no disk read), and drag and drop.

### 8. File sizes

`NewStructureWizardPanel.cpp` went from 1338 lines to 380, split into `…Basis.cpp`,
`…Preview.cpp`, `…Catalog.cpp`, `…Library.cpp`, `…Session.cpp` and `…Symmetry.cpp`
(the unused `…Bonds.cpp` stub was deleted). `RendererPanelToolbar.cpp` went from 858 to 198.

---

## Deviations from the plan

1. **No `DS_STRUCTURE_PATH` drag payload.** ImGui keeps a single payload per drag source, so a
   structure-specific payload would have to overwrite `DS_TREE_ENTRY_PATHS` (the way the WAVECAR
   one does) and cost drag-to-move for every structure file. The wizard accepts
   `DS_TREE_ENTRY_PATHS` instead and takes the first path. No change to `ProjectTreePanel`.
2. **The session window has no `hkl` / display-toggle row.** The plan asks for exactly one
   horizontal and one vertical toolbar; `h×k×l` and the pane checkboxes already live in the
   wizard, so the old `drawSharedToolbar` was deleted rather than kept as a third bar.
3. **`ViewportToolbars.cpp` was split again** into a horizontal and a vertical file to stay near
   the ~500-line project rule.

---

## Not done

- The three headline acceptance cases (diamond → spacegroup 227, BCC iron → 229, rocksalt → 225)
  are covered at the atom-count level by the new unit tests; the **spacegroup** half needs the
  running app and `Show symmetry`, which is a manual check.
- Debug and Dist builds (Release-only during active development, by standing preference).
- Commit and the `architecture-boundary-review` / `full-build-verify` pre-merge sequence.

---

## Follow-up fixes after the first hands-on pass (2026-09-11)

1. **Viewport keybindings were dead in the creation panes.** The panes are not drawn by
   `RendererPanel`, so nothing published `FocusChanged` for them - which left
   `RendererLayer::GetFocusedViewportWindowId()` empty, the `renderer.viewport.focused` keybinding
   context off, and `findViewportCommandWindow` with no target. The focus bookkeeping moved out of
   `RendererPanel` into `UpdateViewportFocusState` (`ViewportInput.{hpp,cpp}`), which both panels
   now call; the creation window reports focus for its ACTIVE pane only.
   `RendererPanel::applyContinuousNudge` (Ctrl+Shift+Arrow glide) still lives in `RendererPanel` and
   is not run for panes - it also needs `m_CommandRegistry`, so lifting it is a separate change.
2. **Add to Project could not re-add a structure whose folder had been deleted.** The duplicate
   `sourcePath` check runs inside `RegisterAsProjectMember`, i.e. AFTER the job has written the new
   POSCAR - so a record left dangling by a Project Tree delete is indistinguishable from a live one,
   and every re-add failed. A pre-flight in `StructureLifecycleCoordinator` now runs before anything
   is written: the file still exists → fail immediately (nothing created, nothing deleted); the file
   is gone → drop the dangling record (new `StructureRegistry::Remove`) and continue. A registration
   that still fails now removes the whole directory it created instead of only the POSCAR, which is
   what used to leave empty folders carrying `.pending_registration` in the tree.

Left over from the old behaviour: `test-directory/generation_test/hBN/` is an empty folder with a
stray `.pending_registration` - safe to delete by hand.


## Second hands-on pass (2026-09-11)

3. **Held-arrow nudge and pan were dead in the panes.** Unlike every `repeatable: true` keybinding,
   `applyContinuousNudge` (Ctrl+Shift+Arrow atom glide) and `applyContinuousPan` (Alt+Shift+Arrow
   camera glide) are polled per frame by whoever draws the viewport - and only `RendererPanel` did.
   Both moved into `ViewportInput` as `ApplyContinuousKeyboardNudge` / `ApplyContinuousKeyboardPan`;
   `RendererPanelInput.cpp` is gone and `StructureCreationTabsPanel` calls them for its active pane.
   The panel now takes `WeakRef<CommandRegistry>` (the nudge commits through
   `renderer.gizmo.commit_transform`).
4. **Add to Project ignored the supercell.** `draftStructure` is the unit cell by design - the middle
   pane renders it - and the Structure Hub submitted it verbatim, so a 2x2x2 request wrote the
   conventional cell. `BuildSessionExportStructure(session)` (App/CreationSession) is now the one
   place `draft (x) supercellCounts` happens, used by both the supercell pane and the hub, and the
   hub's "Atoms:" readout counts the expansion. Three tests in `tests/App/CreationSessionTests.cpp`.
5. **The Export POTCAR checkbox did nothing.** `m_ExportPotcar` was read by nobody and
   `EditorLayer::exportPotcarNextToPoscar` was called by nobody - Step 11 removed the legacy save path
   that used to be the trigger and left both halves stranded. The flag now travels
   `CreationSession::exportPotcar` -> `StructureRecord::exportPotcar` (set by the coordinator after
   registration) -> `EditorLayer::onProjectStructureAdded`, which writes the POTCAR next to the
   POSCAR. A missing `ui.pseudopotential_dir` still surfaces as a pinned error notification.

Release: both targets build, **307 passed / 2 skipped** (309 total).

## Third hands-on pass (2026-09-11)

6. **The generated-atom list is editable again.** The requested "sublattice" feature turned out to be
   a restoration: pick a centering, get the full list of generated atoms, set each one's element
   (F + every atom C = diamond; face centres a different element = an ordered derivative).
   `m_SpeciesOverrides` holds one optional species per generated-atom index and is applied in
   `buildStructure()` right after `ExpandBasisOverLattice`, so positions stay the centering's and
   only the occupant changes. The overrides drop wholesale whenever the generated-atom count changes
   (a basis row added, the centering switched) - the indices would otherwise point at sites the user
   never picked an element for. The table warns that any override makes the real lattice primitive,
   whatever the radio group upstairs still reads, and has a "Reset elements to the basis" button.
   No domain change: this is UI on top of the existing `lattice (x) basis` convolution.
7. **Export POTCAR is greyed out without a pseudopotential directory.** Ticking it with
   `ui.pseudopotential_dir` unset did nothing but pin a "POTCAR export failed" notification, and only
   after the structure had already been added. `EditorLayer::pushPseudopotentialStateToWizard` tells
   the wizard on every config apply (and once at panel registration, since config is normally applied
   before the panels exist); the flag is pushed, not read, because `m_CurrentConfig` is replaced on
   every apply and a reference held by the panel would dangle.

Release: both targets build, **307 passed / 2 skipped** (309 total) - unchanged, both fixes are UI.

## Fourth hands-on pass (2026-09-11)

8. **Selection and the transform gizmo were dead in the creation panes.** Same root cause as the
   keybindings two passes ago, one layer up: picking and the G/R/S gizmo were `RendererPanel`
   members, and the panes are not `RendererPanel` windows, so nothing ran them. Moved out to
   `ViewportPicking.{hpp,cpp}` (`HandleAtomPick` / `HandleViewportPick`) and `ViewportGizmo.{hpp,cpp}`
   (`RenderTransformGizmo`, plus the numeric-override and screen-hit-test helpers only it used) -
   pure moves, the only signature change is `m_Layer` / `m_CommandRegistry` becoming parameters.
   `StructureCreationTabsPanel::drawPane` now runs gizmo-then-pick in the same order and with the
   same `gizmoCapturing` suppression the main viewport uses. `RendererPanel.cpp` drops 4061 -> 3076
   lines; `ViewportGizmo.cpp` is 828, over the ~500 rule because `RenderTransformGizmo` is one
   660-line function and splitting it is a change of its own, not a side effect of this one.
9. **Per-pair bond length is editable in angstrom.** The per-pair override already existed but was
   expressed as a *scale* on the sum of covalent radii, which is not the question anyone asks. The
   Bond Settings panel now edits the cutoff directly ("C-C up to 1.8 A") in both tables, and the
   "Detected in this structure" rows are editable in place instead of needing "Add override" first.
   Storage is unchanged - `BondGenerationSettings::perPairCutoffOverride` is still a scale, since
   that is all `BondGenerator` reads; `RadiusSumForPair` converts at the UI edge.
10. **Bonds crossing the cell boundary can be switched off.** They run to a periodic image nobody
    draws, so on a supercell they read as stubs poking out of every face. New per-window
    `showPeriodicBonds` (toolbar "Periodic", next to "Bonds"), **default on** so nothing changes
    silently - a 2D sheet's edge connectivity is exactly what they were added for. Filtered where
    `cachedBondInstances` is built, with a `lastShowPeriodicBonds` dirty check alongside
    `lastBondRadiusMultiplier`; persisted as its own `show_periodic_bonds=` line rather than a fifth
    field in `show=`, which is only read back when it splits into exactly 4.
11. **The supercell pane draws the unit cell inside it.** `ShowCrystalStructurePreview` already took
    an overlay-cell matrix (added for the primitive-cell overlay); the supercell pane now passes
    `draftStructure.cell` into it, so the cell being edited is outlined in the overlay colour inside
    the block. No new renderer code.

Release: both targets build, **307 passed / 2 skipped** (309 total) - unchanged. A 25-second launch
logged nothing but the pre-existing missing `tool-displacement.png` icon warning. No new tests: 8 and
11 are pure moves/wiring covered by the suite compiling, 9 is arithmetic at a UI edge, and 10 lives
in the GL backend where there is no headless harness.

## Fifth hands-on pass (2026-09-11)

12. **Editing a basis row did nothing.** `pullGizmoEditsFromPreview` copied the basis pane's atom
    positions back into `m_BasisRows` on EVERY frame, not just while a gizmo drag owned them. A
    coordinate typed into the basis table was therefore overwritten by the pane's stale copy in the
    same frame it was typed, so the draft never changed and no preview - unit cell or supercell -
    had any reason to redraw. Now gated on `gizmoDragActive`, plus the one frame after a drag ends
    (the drag clears the flag before it commits the final position), tracked by
    `m_GizmoDragWasActive`.
13. **Bond Settings was dead during creation.** It resolves its target through
    `ResolveAtomEditTarget`, which rejects a window whose `domainStructureId` is empty - which is
    every creation preview pane, by design, since nothing is registered until Add to Project. The
    panel now falls back to the `CreationSession` behind `RendererWindowState::sessionId` and edits
    `draftStructure.bondSettings` / `motifStructure.bondSettings` directly, with no undoable command:
    a draft is not in the project yet. `syncDraftToSession` carries the settings across the
    once-per-frame rebuild of the draft, and `computePreviewSignature` hashes them so a cutoff edit
    re-runs the previews (per-pair overrides summed rather than combined in sequence - it is an
    unordered_map and nothing promises a stable iteration order).

14. **Bond Settings crashed on a creation pane, and moved into the wizard.** The session fallback
    added in 13 only guarded the two places that seeded the edit buffer; the "Detected in this
    structure" table and the reset button still dereferenced the null `domainRecord`. Rather than
    null-guarding a panel that has no business reaching into a draft, the editor block itself moved
    out to `BondSettingsEditor.{hpp,cpp}` (`DrawBondSettingsEditor`, returning true on a finished
    edit) and now has two callers: the panel, for structures in the project, and a new "Bonds"
    section in New Structure, for the draft. The wizard owns `m_BondSettings` and stamps it in
    `buildStructure`/`buildMotifStructure`; no command, no undo entry, and no session lookup - the
    preview signature notices the change and the three panes re-bond themselves. `BondSettingsPanel`
    is back to domain-only and says where the draft's settings live.
15. **The unit cell is outlined across the whole supercell.** A single box in the corner was
    invisible among the atoms. `ShowCrystalStructurePreview` takes an `overlayCellRepeat`, and the
    supercell pane passes its h x k x l, so every cell in the block is outlined - the textbook
    lattice framework. Shared faces are drawn twice; a line VBO does not care. Still one draw call
    in the overlay colour. Not done, and worth asking about only if the framework is not enough:
    shaded faces on one highlighted cell (a translucent pass with sorting) and coordination
    polyhedra (a feature of its own).

Release: both targets build, **307 passed / 2 skipped** - unchanged. Launch clean apart from the
pre-existing missing-icon, Ctrl+Z keybinding-conflict and event-queue-growth warnings.

## Sixth hands-on pass (2026-09-11)

16. **The rest of RendererPanel's per-frame input half moved out, and the panes now run all of it.**
    The pass before this one moved picking and the atom gizmo; everything else that decides what a
    click means still sat as `RendererPanel` members, so the creation panes had no labels, no scene
    arrows, no box/circle select - the same regression shape, one feature at a time. Now in
    `ViewportSelection.hpp` (region select and its ten hit-tests, the label and scene-arrow gizmos,
    the pin/label/arrow click-drag handlers, split across `ViewportRegionSelect.cpp`,
    `ViewportLabelGizmo.cpp`, `ViewportLabelInteraction.cpp`, `ViewportSceneArrowGizmo.cpp`,
    `ViewportSceneArrowInteraction.cpp`), plus the toolbars in `ViewportToolbars.hpp`. Pure moves;
    the only signature change is `m_Layer`/`m_CommandRegistry` becoming parameters.

    The order these run in is the part that kept getting lost, so it exists exactly once:
    `ViewportInteraction.hpp` holds `RunViewportGizmoChain` (label transforms, the unconditional pin
    keyboard shortcuts, then the short-circuiting `||` gizmo/interaction chain) and
    `DrawAndDispatchSelectionTools` (box/circle overlay, the brush's scroll-wheel radius, the drag
    dispatch). `RendererPanel::renderStructureWindow` and `StructureCreationTabsPanel::drawPane` both
    call those two instead of keeping a copy. The selection-tool call stays *before* navigation in
    both: the circle brush consumes the wheel event the camera would otherwise zoom with. Cursor3D
    and the measure tools stay with `RendererPanel` - they open popups and dialogs a pane does not
    own - and are reached through the same `gizmoCapturing || selectionToolConsumedMouse`
    short-circuit that box/circle select used to sit in.

    `RendererPanel.cpp` 3076 -> 889 lines, its header 91. `ViewportGizmo.cpp` (828) and
    `ViewportSceneArrowGizmo.cpp` (591) are over the ~500 rule because each is one long gizmo
    function; splitting those is a change of its own, not a side effect of this one.

Release: both targets build, **307 passed / 2 skipped** (309 total) - unchanged, this is a move plus
wiring. A 20-second launch logged only the pre-existing missing-icon, EventBus-binding, event-queue
and Ctrl+Z keybinding-conflict warnings.

## Seventh hands-on pass (2026-09-11) - four reported viewport defects

17. **The cell box and the lattice framework were uploaded once and never again.**
    `OpenGlRendererBackend` raised `cellEdgesDirty` only when a window's *source path* changed.
    Every structure creation pane carries an empty path by construction (a preview is deliberately
    not a domain record), so the flag never fired again after the first frame: the white cell box
    kept its opening size through every lattice-constant edit, and the blue overlay - the primitive
    cell in the unit-cell pane, the whole lattice framework in the supercell pane - never appeared
    at all when the supercell counts were raised after the window already existed. Atoms and bonds
    were never affected because they have a position hash; the cell edges had nothing equivalent.

    Fixed by deleting the flag rather than adding a hash: `renderCellBox` already re-uploads the
    whole line buffer with `glBufferData` every frame, so gating only the CPU-side copy bought
    nothing and cost a staleness bug.

18. **`BuildSupercell` dropped `bondSettings`.** It carries `isPeriodic`, the cell and the atoms,
    but not the cutoffs, so a supercell always re-bonded at the stock 1.18 global scale - every
    per-pair bond length set on the unit cell was silently ignored in the supercell pane and in the
    exported file. One line, plus `SupercellTests.BondSettingsCarryOverToTheSupercell`.

19. **A pane swallowed the click that activated it.** `drawPane` computed `isActive` from
    `session.activePaneIndex` *before* the "clicking a pane makes it active" line below it, so the
    press that activated a pane ran the whole input chain with `isActive == false`. A plain pick
    survived this (the next click landed on an already-active pane), but box/circle select did not:
    the drag starts on `IsMouseClicked`, and by the frame the pane went active the button was
    already down, so the drag never began and no selection rectangle was ever drawn. The activating
    click is now applied before `isActive` is read; the border colour and the focus event still use
    last frame's answer, since both are emitted before the viewport image exists.

Release: both targets build, **308 passed / 2 skipped** (310 total - the one new test).
`JobSystemControlTests.RetryResubmitsFinishedJob` failed once under load and passed 5/5 on its own
and on a clean re-run - timing flake, unrelated to this pass. A 20-second launch opened the three
creation panes and logged only the pre-existing missing-icon warning.

20. **The cell box looked sliced, not terminated.** A cell holds its atoms on `[0, 1)` per axis, so
    the far faces carry none at all: in a rock-salt supercell the top row of cations - the ones that
    close the cells - simply was not there, leaving the anions below them bonded to nothing.
    `ShowCrystalStructurePreview` now adds the boundary images (VESTA's own behaviour) before
    bonding: one image per non-empty subset of the axes an atom sits on the zero-face of, so a
    corner atom gains all seven and a face atom exactly one.

    Display only, and only for the panes that draw a cell box. It is never applied to what gets
    exported - the images are duplicates, and a POSCAR built from them would carry the wrong
    stoichiometry - and the basis pane is excluded because its atom list has to stay one-to-one with
    the wizard's basis rows for the gizmo read-back to trust it.

## Still open

- Symmetry detection in the fourth tab (cell / atomic basis readout) is not right yet. Deferred by
  request, not investigated. It is also the natural verifier for item 6 (an ordered F structure must
  come back as its real, lower-symmetry spacegroup).
- Library prototype entries for the orderings item 6 makes typeable (CsCl B2, Cu3Au L1_2,
  chalcopyrite CuFeS2) - none of them need new machinery, only YAML.
- Ordered derivatives (enumerate all inequivalent decorations of a site, pymatgen-style) remain a
  task of their own, not started.
