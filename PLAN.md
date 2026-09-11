# Plan: Structure Creation UI Redesign — lattice × basis, single-window 3-pane preview

_Locked via grill — by Claude Opus 5 + pzabier@gmail.com, 2026-09-10_

Branch: `task/18-structure-lifecycle-redesign`.

The previous plan (Structure Lifecycle Refactor, Steps 10-11) is archived at
`docs/work/project/plans/2026-09-08-structure-lifecycle-steps-10-11.md`. Its session/registry model,
event contracts, path validation and atomic staging-write contract are **implemented and remain
binding** — this plan changes only the crystallography model and the creation UI on top of them.

---

## Goal

Fix two structural mistakes in the structure-creation UI and finish the surrounding UX.

The first is a domain error: the panel conflates the **Bravais lattice** with the **atomic basis**.
Today the `Face-centered (F)` button *overwrites* the basis table with the four FCC lattice points and
calls them atoms, so a user who wants diamond gets simple-cubic-with-FCC-atoms (spacegroup 221 Pm-3m)
instead of diamond (227 Fd-3m), and would have to hand-type eight rows to get the real thing. A
crystal structure is `lattice ⊗ basis`: diamond is the FCC lattice with a two-atom carbon basis at
`(0,0,0)` and `(¼,¼,¼)`. Centering becomes a property of the lattice section; the basis table holds
only the motif; `buildStructure()` convolves them.

The second is a UI-architecture error: the three preview views are three separate renderer windows
docked side by side, so the user sees three title bars and three full toolbar sets. They must be
three *panes inside one renderer window* sharing one horizontal and one vertical toolbar.

Alongside those: previews must exist from the first valid draft rather than waiting for the Structure
Hub hand-off, file loading must use real pickers instead of a raw text field, and the mode's name
must be the same string everywhere.

---

## Approach

### 1. Domain — separate lattice from basis

`Domain/Crystal/BravaisLattice.hpp:65` already exposes
`GetCenteringPresetBasis(BravaisCenteringPreset) -> std::vector<glm::vec3>`, which returns **lattice
points**, not atoms. The name is part of the confusion.

1. Rename it to `GetCenteringTranslations()` so the return value says what it is. Keep the same
   values (`P` → `{(0,0,0)}`, `I` → `+(½,½,½)`, `F` → `+(½,½,0),(½,0,½),(0,½,½)`, `C` → `+(½,½,0)`).
2. Add `Domain/Crystal/LatticeBasisExpansion.{hpp,cpp}`:
   ```
   [[nodiscard]] std::vector<AtomSite> ExpandBasisOverLattice(
       std::span<const AtomSite> basis,
       BravaisCenteringPreset centering);
   ```
   For each centering translation `t`, for each basis atom `b`: emit an atom at
   `frac(b.fractional + t)`, species carried through. Wrap into `[0,1)`. Result count is
   `translations × basis`.
3. `NewStructureWizardPanel::buildStructure()` calls this instead of turning `m_BasisRows` straight
   into atoms. `m_BasisRows` is now the motif only.

**Diamond acceptance case:** Cubic, `a = 3.567`, centering `F`, basis rows `C(0,0,0)` and
`C(¼,¼,¼)` → 8 atoms, spacegroup 227 (Fd-3m) from the existing `Show symmetry` button. This is the
single test that proves the model is right; it fails today.

### 2. NewStructure panel — three sections

- **Lattice section** — crystal system, `a/b/c`, angles, and now the centering radio group
  (`Primitive (P)` / `Body-centered (I)` / `Face-centered (F)` / `Base-centered (C)`). `IsPresetSupportedFor()`
  still greys out presets the system does not admit. Selecting a centering no longer touches the basis.
- **Atomic basis section** — the motif. Unchanged widget (`drawBasisTable()`), changed meaning: these
  are the atoms attached to *one* lattice point. Header text must say so.
- **Generated atoms** — new collapsing header, collapsed by default, listing every atom
  `ExpandBasisOverLattice` produced with its fractional coordinates. Read-only. This is what goes to
  the renderer and to POSCAR, so it is the user's check that the convolution did what they meant.

`drawCenteringPresetRow()`'s current body (clear `m_BasisRows`, refill from the preset) is deleted —
that mutation is the bug.

### 3. Session lifecycle — previews from the first valid draft

Currently `StructureCreationTabsPanel::Render()` skips sessions in `Draft` state, so nothing appears
until "Move to Structure Hub" flips the state to `Ready`. Change:

- `NewStructureWizardPanel` creates its session as soon as the draft is valid — at least one basis row
  with a non-empty species — not on hand-off. `syncDraftToSession()` already runs every frame and
  keeps the registry copy current.
- The creation window renders for `Draft` sessions too. It is the draft's viewport for the whole
  editing session.
- **"Move to Structure Hub" no longer opens anything.** It publishes `SessionReadyForStructureHub`,
  which sets `state = Ready` and puts the draft on the Hub's list with its `Add to Project` button.
  The button label reflects this: it is a submit action, not a window-opening action.
- Closing the New Structure panel still publishes `RendererTabClosed` (the shared idempotent close
  path from the archived lifecycle plan, Section 7) — unchanged.

### 4. Renderer — one window, three panes

This is a rewrite of `Presentation/Panels/StructureCreationTabsPanel.cpp`. The DockBuilder 2+1 layout
and the `m_SessionDockNodes` / `m_LaidOutSessions` bookkeeping all go away.

Established from the code, and what makes this cheap: a `RendererWindowState`
(`Renderer/RendererWindowState.hpp:40`) owns its own `RendererViewCamera` and `viewportSize`, and
`RendererLayer::RenderToFbo(windowId, structure, windowState, globalSettings)` returns a texture id.
`RendererPanel::renderStructureWindow()` is just `Begin` → `drawViewportToolbar` →
`drawViewportVerticalToolbar` → `SetViewportSize` → `RenderToFbo` → `ImGui::Image`. Three panes
therefore need three `RendererWindowState`s for camera+FBO, but only one ImGui window and one
`ImGui::Image` per pane.

1. **Exclude session windows from the normal loop.** `RendererPanel::render()` iterates
   `m_Layer.GetWindows()` unconditionally (`RendererPanel.cpp:174`). Skip any window whose
   `sessionId` is non-empty — those are drawn by the creation panel. The existing
   `dockingInitialized` special-case for session windows at `RendererPanel.cpp:212` becomes dead and
   is removed.
2. **Expose the two toolbar draw calls and the viewport draw** so the creation panel can reuse them
   rather than duplicating. `drawViewportToolbar` / `drawViewportVerticalToolbar` are currently
   private members of `RendererPanel`; lift the shared body into a small
   `Presentation/Panels/ViewportToolbars.{hpp,cpp}` free-function pair taking
   `(RendererWindowState&, RendererLayer&)`. Both panels call it. No behaviour change for normal
   windows.
3. **The creation window body:**
   ```
   Begin("<StructureName> (h x k x l)###StructureCreationSession_<sessionId>")
     DrawViewportToolbar(activePane)          // one horizontal toolbar
     Separator
     DrawViewportVerticalToolbar(activePane)  // one vertical toolbar
     SameLine
     BeginChild("panes")
       top row    (child, height = 1 - bottomFraction)
         BeginChild("basis",     width = leftFraction) -> Image(RenderToFbo(basisWindow))
         vertical splitter
         BeginChild("unitcell",  rest)                 -> Image(RenderToFbo(cellWindow))
       horizontal splitter
       bottom row (child)
         BeginChild("supercell", full width)           -> Image(RenderToFbo(superWindow))
     EndChild
   End
   ```
   Layout: **basis + unit cell on top, supercell full-width below.** Increasing size order; the
   supercell is the one that grows to hundreds of atoms and needs the width.
4. **Splitters** are draggable; `leftFraction` and `bottomFraction` live in `CreationSession` so a
   session remembers its own proportions.
5. **Active pane.** Clicking a pane makes it active; it gets a highlight border. The toolbars and
   mouse input act on the active pane only, and **each pane keeps its own camera** — rotating the
   supercell must not move the unit-cell view. Store `activePaneIndex` in `CreationSession`.
6. **Pane visibility.** The three checkboxes in NewStructure hide a pane; the remaining panes stretch
   to fill (hide the basis → unit cell spans the top; hide two → the last one takes the window). A
   hidden pane no longer destroys and recreates a renderer window — it is now purely a layout
   decision, which also removes the `DockBuilderRemoveNode` crash this replaces.

**The three panes:**

| Pane | Content |
|------|---------|
| Basis | The motif alone — `m_BasisRows` as atoms, no centering expansion, no cell box |
| Unit cell | The conventional cell — full expanded structure, cell box, optional primitive-cell overlay |
| Supercell | `BuildSupercell(unitCell, h×k×l)`; at `1×1×1` it shows the same content as the unit cell rather than disappearing, so the layout never jumps |

`Show primitive cell` stays, as a contrasting second cell frame drawn inside the unit-cell pane
(`CreationSession::primitiveCellOverlay`, already plumbed). Now that centering is an explicit lattice
property it is always known, so the checkbox stops being greyed out for catalog structures.

### 5. File selection — four routes

`Core/Platform/FileDialog.cpp` already wraps NFD (`Vendor/nativefiledialog-extended`, built in
`premake5.lua:250-263`) but only exposes a folder picker (`NFD_PickFolderN`). All four routes below
feed the same `dispatchFileLoad()` → `OpenDefectJob` (PuntukasBridge) → `adoptLoadedStructure()` path
that already exists.

1. **NFD open-file dialog** — add `PickFile(filters)` next to the existing `PickFolder`, using
   `NFD_OpenDialogN` with filters for `POSCAR`/`CONTCAR`/`*.vasp`/`*.cif`. `Browse` button.
2. **Project Tree selection** — "Use Project Tree selection" button. The panel subscribes to
   `ProjectTreeSelectionChanged` (already defined in `Core/Domain/StructureLifecycleEvents.hpp`) and
   keeps the last selected *file* path.
3. **Active renderer window** — "Use active viewport" button: copies the structure out of the
   last-focused `RendererWindowState` directly, no disk read and no Python round-trip.
4. **Drag and drop** onto the panel. `RendererPanel` already has the pattern with the
   `DS_WAVECAR_PATH` payload; add a `DS_STRUCTURE_PATH` payload emitted by `ProjectTreePanel` for
   structure files.

`adoptLoadedStructure()` must now also populate the *basis* correctly: a loaded file gives a full
atom list, not a motif. It sets centering to `Primitive (P)` and puts every loaded atom in the basis
table — `P` has a single identity translation, so `lattice ⊗ basis` reproduces the file exactly.
Recovering a smaller motif from a loaded structure is a symmetry-detection problem (spglib), not
arithmetic, and is out of scope.

### 6. Naming

`ToString(CreationMode::FromScratch)` returns `"Create New"`, matching the tab. Same for the other
three (`"From Library"`, `"Import File"`, `"Analyze Existing"`). The enum keeps its internal names.
The creation window title is `<structure name> (h×k×l)`.

### 7. From Library

Selecting a prototype fills the lattice section (system, parameters, centering) **and** the basis
table from the catalog, and everything stays editable — pick `Diamond`, change the second basis
atom's species to `Zn`, and you have zincblende. Same `lattice ⊗ basis` model as Create New, just
pre-filled. `applyPrototypeToBasis()` is rewritten to split the prototype's conventional-cell
positions into centering + motif rather than writing all positions as basis rows.

---

## Key decisions & tradeoffs

| Decision | Rationale | Tradeoff |
|----------|-----------|----------|
| **Centering is a lattice property; basis is the motif** | It is the actual crystallography. Diamond becomes FCC + 2 rows instead of 8 hand-typed rows, and the spacegroup comes out right. | Existing saved drafts that used the old "centering fills the basis" behaviour will re-expand and gain atoms. No such drafts are persisted today (drafts are ephemeral), so no migration is written. |
| **Read-only generated-atoms list, collapsed by default** | The convolution is invisible otherwise — the user types 2 rows and 8 atoms reach POSCAR. | One more widget and a per-frame expansion when expanded. The expansion is `translations × basis`, trivially small at unit-cell scale. |
| **One window, three panes, one toolbar set** | Directly what was asked, and three toolbar sets ate most of the vertical space. | The panes are no longer independently dockable. Accepted deliberately: they are views of one structure, not three documents. |
| **Camera per pane, toolbar acts on the active pane** | Rotating the supercell must not disturb the unit-cell view; comparing two orientations is a real need. | A user who *wants* locked cameras has to orbit twice. A "link cameras" toggle is a cheap later addition, not built now. |
| **Previews from the first valid draft, not from hand-off** | The preview is the feedback loop for editing; making it wait until submission inverts the workflow. | A session exists earlier, so `CreationSessionRegistry` holds drafts that may never be submitted. They are removed by the same idempotent `RendererTabClosed` path. |
| **"Move to Structure Hub" becomes submit-only** | With previews already open, the button's only remaining job is putting the draft on the Hub's list. | Two-step commit (Move, then Add to Project) survives. Kept because the Hub is where the target folder is confirmed. |
| **Supercell pane persists at 1×1×1** | The layout must not jump every time h×k×l crosses 1. | Two panes briefly show the same content. Cheaper than a re-laying-out window. |
| **Loaded files land in the basis under `P` centering** | Reproduces the file byte-exactly with no symmetry guessing. | No motif reduction on import — a loaded FCC file shows 4 basis rows, not 1. Correct, just not minimal. |
| **All of it in `task/18`** | The domain fix and the layout fix both change what `buildStructure()` feeds the renderer; splitting means merging a state that is still wrong. | One larger review. Mitigated by the acceptance list below. |

---

## Risks / open questions

1. **`RenderToFbo` three times per frame.** Three FBOs at pane resolution instead of one at window
   resolution. Panes are smaller than a full window, so the pixel count is comparable, but this is
   unverified — measure before assuming, and reuse the existing Tracy instrumentation.
2. **Toolbar extraction touches normal renderer windows.** Lifting `drawViewportToolbar` /
   `drawViewportVerticalToolbar` out of `RendererPanel` risks regressing every ordinary viewport.
   The functions must move without edits; any behaviour change belongs in a separate commit.
3. **`ExpandBasisOverLattice` and overlapping atoms.** A user can type a basis atom at `(½,½,0)` with
   `F` centering and land two atoms on the same site. Detect coincident positions and warn in the
   generated-atoms list — do not silently deduplicate, since the user may be mid-edit.
4. **`applyPrototypeToBasis` splitting prototypes into centering + motif** is the one genuinely
   uncertain piece: `prototypes.yaml` stores conventional-cell positions, and factoring them back
   into centering × motif is a small pattern match, not a general algorithm. If a prototype does not
   factor cleanly, fall back to `P` + all positions (correct, just not minimal) and log it.
5. **Splitter fractions in `CreationSession`** put UI layout state in an App-layer type. Acceptable —
   it is per-session view state with no domain meaning — but it is a boundary smell worth noting.

---

## Out of scope

- Symmetry-based motif reduction on import (finding the minimal basis of a loaded structure).
- Linked cameras across panes.
- User-configurable pane arrangements beyond the fixed 2-over-1 and hiding panes.
- Non-diagonal supercell matrices (`SupercellMatrix::Diagonal` only, as today).
- Anything in the archived lifecycle plan that is already implemented: session registry, event
  contracts, path validation, the staging-directory atomic write, `Add to Project`, ProjectTree
  integration.
- Export formats other than POSCAR.

---

## Acceptance

- [ ] **Diamond:** Cubic, `a = 3.567`, centering `F`, basis `C(0,0,0)` + `C(¼,¼,¼)` → 8 atoms,
      `Show symmetry` reports spacegroup 227 (Fd-3m).
- [ ] **BCC iron:** Cubic, centering `I`, basis `Fe(0,0,0)` → 2 atoms, spacegroup 229 (Im-3m).
- [ ] **Rocksalt:** Cubic, centering `F`, basis `Na(0,0,0)` + `Cl(½,½,½)` → 8 atoms, spacegroup 225.
- [ ] Selecting a centering never modifies the basis table.
- [ ] Generated-atoms list matches the atom count and positions in the written POSCAR.
- [ ] The creation window has exactly one horizontal and one vertical toolbar.
- [ ] Three panes: basis and unit cell on top, supercell full width below; splitters drag.
- [ ] Clicking a pane makes it active; orbiting it leaves the other panes' cameras untouched.
- [ ] Unchecking a view collapses its pane and the others stretch; re-checking restores it. No crash.
- [ ] Panes appear as soon as the first basis row has a species — before any Structure Hub hand-off.
- [ ] `Move to Structure Hub` opens no window; it puts the draft on the Hub list as `Ready`.
- [ ] All four file-selection routes load a POSCAR into the basis table.
- [ ] The mode reads `Create New` in the tab, in the session state line, and in the Hub list.
- [ ] Release build green; test suite still 293 passed / 2 skipped, plus new
      `LatticeBasisExpansionTests` covering P/I/F/C expansion, wrap-around, and coincident-atom
      detection.

**Build and test only through `scripts/Windows/BuildErrorsOnly.bat` and `scripts/Windows/Build.bat`
— never raw MSBuild.** Release configuration only during active development.
