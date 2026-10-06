# New Structure wizard — design decisions

Repository: `Defect-Studio-mgr`
Date: 2026-09-02
Status: decided, not yet implemented
Method: design interview against the running application, every claim checked in the code

Written in English to match `docs/adr/` and the rest of the agent-facing documentation set. The
decisions below came from a design review of `NewStructureWizardPanel` and grew to cover structure
persistence, transforms and the undo model.

Companion documents: `docs/remediation-plan-2026-09-02.md` (architecture and test debt),
`docs/adr/` (the standing architectural decisions these must not violate).

## Ground rules

- **TDD is a project decision.** Every behaviour change starts with a failing test.
- Only the main thread commits state visible in the project or UI.
- `Domain` depends on nothing but `Core`. Nothing here may change that.
- Errors are `Result<T>` + `StructuredError`, never exceptions on a rendering path.

---

## What the code actually does today

Established by reading the current tree, because several of these contradict what the panel appears
to do:

| Claim | Reality |
|---|---|
| Point group is missing | It is **already rendered** — `NewStructureWizardPanel.cpp`, `"Point group: %s"`, behind the `Show symmetry` button |
| Preview and Create are different operations | Both call `OpenCrystalStructureAsWindow` (`:381`, `:394`); each click registers a **new** `StructureRecord` and opens a **new** window |
| Supercell generation needs building | `SupercellMatrix::Diagonal(n,k,l)` and `BuildSupercell` already exist in `Domain/Crystal/Supercell.hpp` |
| The renderer cannot show two views side by side | It can — every `RendererWindowState` is its own `ImGui::Begin` (`RendererPanel.cpp:170,206`) in the global DockSpace. Nothing tells ImGui *where* to dock a new one, so it lands as a tab |
| The periodic table is a wizard problem | `DrawPeriodicTableGrid(..., cellSize = ImVec2(38,32), ...)` is shared by `ElementCatalogPanel`, `RendererPanel::drawAddAtomPopup`, `drawPeriodicTableWindow` and the wizard |
| A 3D pivot has to be built | `cursor3DPosition`, `cursor3DPlaced`, `SelectionToolMode::Cursor3D` and `Cursor3DSetPositionRequested` exist and are renderer-only (`RendererEvents.hpp:358`) |
| A persistent Python worker needs building | `Core/Platform/InteractiveProcess.hpp` already has `Start` / `WriteLine` / `PollOutput` / `IsRunning` / `Terminate` |
| There is no dirty-state machinery | `UndoStack::MarkClean()` / `IsClean()` exist, used only by `Demo/DemoBackendRuntime.cpp` |
| Species are entangled with the defect pattern | They are already separate — `PointDefectOperation` carries `type` independently of `atom.species` and `replacementSpecies` (`Domain/Defects/DefectModel.hpp`) |
| `Ctrl+S` is free | Taken by `project.save` (`keybindings.yaml:36`, `CoreLayer::registerSystemCommands`) |

---

## Decisions

### Live preview

**The wizard owns one ephemeral preview window.** It keeps that window's `windowId`. Every field
change rebuilds the `CrystalStructure` and overwrites that window. The structure is **not**
registered in `StructureRegistry` until `Create`.

*Why not register it immediately:* it would fill the domain registry and the undo history with
structures the user never accepted, and `Ctrl+Z` after `Create` would have to walk back through
dozens of slider micro-edits.

*Consequence:* a new function alongside `OpenCrystalStructureAsWindow` — "update this existing
window" rather than "open a new one".

**`Preview basis only` stops being a button.** It becomes a checkbox on the preview, because its only
difference from `Create` today is the `showCellBox` / `showGrid` flags. That button is the direct
cause of the duplicate-window complaint.

### Split viewport

**Two docked renderer windows, placed side by side with `ImGui::DockBuilderSplitNode`.** Not a real
sub-viewport inside one `RendererWindowState`.

*Why not the real thing:* `RendererWindowState` has 133 fields and is already flagged for splitting
in the architecture review; the render path, picking, gizmo and selection all assume one window =
one camera = one FBO. That is weeks of work for the same picture on screen. **Accepted as a
deliberate interim** — the maintainer expects to revisit it once this lands.

**Mechanics (verified against upstream ImGui docs):** `DockBuilder*` needs `imgui_internal.h`, must
run **before** the dockspace and its windows are submitted, and must be guarded with
`DockBuilderGetNode(id) == nullptr` so the layout is not rebuilt every frame. This means the split
belongs in `ImGuiLayer`, ahead of its `DockSpaceOverViewport` call — not inside the panel.

**Layout:** left pane shows the unit cell (with the primitive-cell overlay). The right pane shows the
supercell and **appears only when n,k,l differ from 1,1,1**; it gets the larger share, and the
divider is resized by the user with the normal ImGui handle.

### Symmetry readout

**Split by cost.** The **lattice point group** follows from crystal system plus centering, so it is
computed locally and shown always, live, for free. The **full space group and Wyckoff letters** stay
on `GetSymmetryInfoJob`, debounced (~750 ms after the last edit) with the in-flight job cancelled.

*Why:* `GetSymmetryInfo` is a Python subprocess and, with `DS_PYTHON_CAPI_AVAILABLE=0`, pays a cold
spglib import every call. Live-per-keystroke is impossible; the half the user actually wanted to see
without clicking happens to be the free half.

### Transforms and the gizmo

**The gizmo writes back into the wizard's fields.** Dragging an atom updates its fractional
coordinates in the basis table. The fields stay the single source of truth.

*Why it cannot work any other way:* `ResolveAtomEditTarget` (`RendererAtomEditCommands.cpp:41`)
requires a non-empty `domainStructureId`, a valid UUID and a registered `StructureRecord`. An
ephemeral preview has none, so every atom-edit command would return
`renderer.atom_edit.no_domain_structure`. And even if it worked, the next slider move would rebuild
the structure from the fields and erase the edit.

**Pivot is an enumeration:** selected atom · 3D cursor · selection centre. **Axis frame is an
enumeration:** global XYZ · lattice `a`/`b`/`c`. Both carry Empty as an **unimplemented case** so
adding it later is nearly free. Neither may be hardcoded to a single choice.

*Why lattice axes matter:* "rotate 90° about global Y" is meaningless in a hexagonal cell; "rotate
about `c`" is the operation a crystallographer actually wants. Global XYZ is included because it is
free.

**Keyboard model is copied from the previous DefectStudio** (`docs/archive/work/project/old-ds-functionality.md`
§3.2): `G` / `R`, then `X`/`Y`/`Z` to bind an axis, `Shift+X/Y/Z` to lock a plane. The user already
knows it, and `Core/Input` plus the keymap already support it.

**Gizmo gestures on registered structures use `UndoStack::ScopedGroup`** so a whole drag is one undo
step, not one per frame.

**`Ctrl+Z` does nothing inside the wizard.** Wizard fields are local UI state, explicitly exempt from
the command runtime by ADR-011 §2. Nothing is pushed on any stack, so there is nothing to group.

### Choosing a cell type

**Two levels, kept visibly separate**, because a Bravais centering is a *lattice* and "diamond" is a
*lattice plus a basis* — they cannot share one list without lying:

- **Structural prototype** (top, primary): sets crystal system, centering and basis together.
- **Raw lattice** (below, collapsed): `CrystalSystem` plus centering, spelled out as
  `Primitive` / `Body-centred` / `Face-centred` / `Base-centred` — never `P` / `I` / `F` / `C`.

**Primitive cell: an overlay, not a transformation.** A checkbox draws the primitive cell edges
inside the conventional cell; the structure is unchanged. For a lattice with a *chosen* centering the
primitive vectors are a textbook formula — local, instant, no Python. The checkbox is greyed out when
the basis cannot be recognised as a clean Bravais lattice, because that case needs spglib. Promoting
the overlay to its own pane later is small, since `DockBuilder` is already in by then; going the
other way would not be.

**No space-group-driven cell selection.** A space group is only useful together with Wyckoff-position
input, which is not this wizard's model. The useful direction already exists and points the other
way: build the basis explicitly, let spglib *read* the group back.

### Prototypes, materials and lattice constants

**Three levels: material → polytypes → per polytype, a prototype plus lattice constants per
functional.**

- **Prototype** — pure topology, no chemistry. `zincblende` = cubic, F-centred, sites `A` at (0,0,0)
  and `B` at (¼,¼,¼); free parameter `a`. One prototype serves ZnS, GaAs, 3C-SiC and cBN.
- **Material** — chemistry plus a prototype per polytype. `SiC` = { `3C` → zincblende; `2H` →
  wurtzite; `4H`, `6H` → their own prototypes }.
- **Constants** — per polytype, `a` (and `c`, and `u` where applicable) separately for `exp`, `PBE`,
  `HSE06`, `r2SCAN`.

**Two YAML files, one loader:** prototypes (topology, built-in, rarely touched) and materials
(chemistry and constants, built-in **and** user-authored). The loader returns
`std::vector<PrototypeDefinition>` and `std::vector<MaterialDefinition>`; nothing above it knows the
storage is YAML, so moving to a real database later changes only the implementation.

**Deliberately not `ase.db` / `MaterialLibraryIO`.** That class exists and is unused, but it is a
subprocess wrapper — **every list read launches Python**, which is unusable for a dropdown redrawn
every frame — and it stores concrete `Atoms`, while a prototype is parameters. It remains the right
home for the user's saved concrete structures, which is a different thing.

**v1 prototypes:** diamond, zincblende, wurtzite, rocksalt, hBN. All two- or four-atom bases, all
analytic.

**Bond length is a property of the prototype.** `d = a·√3/4` for diamond and zincblende, `a/2` for
rocksalt, `a/√3` in-plane for hBN. The field is **bidirectional** — type `d` and `a` is recomputed,
type `a` and `d` refreshes — with the field that is not active this frame recomputed from the other,
never both at once. Greyed out until a prototype is selected. The generic numeric approach (find the
shortest contact and rescale) is rejected: with three or more species it cannot know which pair the
user meant, and would silently distort the rest.

**Formula → site assignment.** The prototype declares named sites with multiplicities; a formula is
matched by multiplicity, and where multiplicities tie, by the order in the formula (`BN` puts B in
the first site, which matches the cation-first writing convention). The resolved mapping is shown as
an **editable table** under the formula field, so an ambiguous case is one click to fix rather than a
silent error. When the user corrects a mapping, **ask whether to remember it** for that material.

*Chemical heuristics were rejected:* electronegativity misclassifies intermetallics and carbides, and
silently swapping the sites in SiC is exactly the "quietly produce wrong science" failure class.

### Lattice constants on export

**One direction only.** The lattice constant belongs to the unit cell; the supercell is always derived
from it. **An imported supercell POSCAR never updates a material's constant.** Recovering it would
require knowing the original n,k,l, which POSCAR does not carry — and a mistake there writes a
constant wrong by a whole factor into the material library, silently.

**The structure remembers which functional it was built from**, and the save dialog can override it.
Without this you cannot tell, looking at an open POSCAR, whether `a = 5.65` is experimental or PBE.
A project-wide setting was rejected: a comparison project holds structures from several functionals
by definition.

**Changing the functional when saving a supercell:** rescale the vectors keeping fractional positions
(correct for an unrelaxed structure), and **refuse with a `StructuredError` for a relaxed one** —
rescaling distorts a relaxation, rebuilding discards it, and neither is what the user meant. Refusal,
not an assertion: the maintainer wants this reopened if more experienced users say the case is real.
Requires one flag on the structure: whether positions came from a relaxation.

### Saving structures and text files

**The writer goes through Python (ase / punktukas), not a hand-written C++ writer.** Those libraries
are documented and tested upstream; our side needs only integration tests; and it stays consistent
with the planned interactive scripting and IPython-style debugging with live variables.

**Now: a one-shot subprocess. Later: the Python daemon.** The C++ API is identical either way, so the
switch touches one file. This is acceptable because saving is explicit and infrequent
(see the shortcuts below), not per-keystroke.

**Non-negotiable regardless:** write to a temporary file and `rename` into place, with a timeout and
a `StructuredError` when the interpreter is dead. A hung or half-written POSCAR is worse than a
refused save.

**Shortcuts:**

| Chord | Action | Note |
|---|---|---|
| `Ctrl+S` | Save the whole project — total persistence, every change | Already bound to `project.save`; extend it, do not add a second binding |
| `Ctrl+Shift+S` | Save Project As | Currently free |
| `Ctrl+Alt+S` | Save the focused single file (structure or text document) | Currently free |

`Ctrl+S` meaning "everything" inverts the text-editor convention deliberately: this is a
project-centric application, the same way `Ctrl+S` saves a whole `.blend`.

**Saving must not clear the undo stack.** Call `UndoStack::MarkClean()`, never `Clear()`. `MarkClean`
only marks a clean point — the history survives, undoing past the save point still works, and
`IsClean()` correctly reports dirty again afterwards.

**A dirty flag is introduced, separate from the undo stack.** `UndoStack` is a single global object,
so `IsClean()` answers "the application has unsaved changes", not "this POSCAR is dirty". With three
structures and two text documents open, five independent answers are needed. Shape: a revision
counter on `StructureRecord` and on each `TextEditorPanel` document, bumped by every change, compared
against the revision recorded at save time. Dirty is shown as `*` (or a bold title) on the window.

**Atom ordering.** Default is the canonical sort by count; a stored user preference wins. A popup asks
once and the answer is remembered.

- **Key:** host prototype plus the **set** of `PointDefectType` values in
  `DefectConfiguration::operations`. Species-agnostic by construction, because
  `PointDefectOperation::type` is already separate from `atom.species` and `replacementSpecies`.
  So `SiNV` and `GeNV` share an entry, and so do `SiN₂V` and `SiN₃V` — only the count of a role
  changes, not the set of roles.
- **Value:** a permutation of *roles*, not of species, so it replays when the dopant changes.

POSCAR/POTCAR ordering cannot desynchronise as long as both are produced from the same sorted object
in one operation.

**POTCAR generation is a checkbox in the save dialog**, greyed out with an explanation until
puntukas' `pseudodir` is configured. The rewrite loads datasets with
`VaspInput.set_potentials()` and writes them with
`puntukas.vasp.potcar.write_potcar(path, list(inp.potcars.values()))`. DefectStudio needs **no**
pseudopotential path of its own — puntukas discovers `paths.json`'s `pseudodir` under its VASP user
config directory after checking `PUNTUKAS_VASP_PP_PATH` and `VASP_PP_PATH`.

*Why a checkbox and not automatic:* bundling POTCAR into every save would make saving a structure
impossible on a machine without a pseudopotential library. Full VASP input preparation
(INCAR, KPOINTS) is a later, separate feature and this does not block it.

### Undo model

**One `UndoStack` class, two instances:** the global domain stack, and one per renderer window. The
hand-rolled `viewUndoHistory` / `viewRedoHistory` vectors in `RendererWindowState`, and the separate
pinned-measurement history, are folded into it.

**The per-window stack is deliberate and stays.** View changes in one window must not undo in
another. What broke was not the two-stack model — it was the pinned bond labels acquiring a third,
independent history with its own shortcut.

**A single shared stack with per-window filtering does not work**, though the instinct behind it is
right. An undo stack must unwind in reverse order of application; picking "the newest record
belonging to this window" out of a mixed stack skips over records from elsewhere. That is safe only
when the scopes are mutually independent — and mutually independent scopes are exactly what separate
stacks are. The real win the idea points at is one implementation instead of three, and that is what
gets built.

**Resulting rule, one sentence:** `Ctrl+Z` undoes changes to the structure; `Ctrl+Alt+Z` undoes what
you did in this window.

### UI details

- **Periodic table:** compute the cell size from `ImGui::CalcTextSize` plus padding instead of the
  hardcoded `ImVec2(38,32)`. One change in `PeriodicTableGrid.hpp` fixes all four panels that share
  it, and survives a font or DPI change.
- **Lattice parameters:** an `ImGui::BeginTable` with label on the left and `DragFloat` on the right,
  as two tables side by side — `a`/`b`/`c` on the left, `α`/`β`/`γ` on the right. One row of lengths
  above one row of angles breaks in a narrow panel.
- **Basis table:** `ImGuiTableColumnFlags_WidthFixed` with one shared width for `x`/`y`/`z`.
- **Species placeholder:** empty with a `(select)` hint and `Create` disabled until every row has a
  species. `X` reads as a delete affordance; a default of `Si` would quietly put silicon in
  structures nobody asked for.

---

## Deferred, with the reason

| Deferred | Why now is the wrong time |
|---|---|
| **Empty scene object** — helper point with local axes, in the scene outliner, serialized to YAML | Its own task, comparable in size to the wizard. The pivot and axis enumerations above leave the seam so it lands nearly free. See `docs/archive/work/project/old-ds-functionality.md` §8.1 |
| **Persistent Python worker** | Touches every bridge, so folding it into a wizard task entangles two independent pieces of work. Target shape: fast queries only (symmetry, matrix suggestions, structure writes); heavy reads (WAVECAR, VASP output) stay on one-shot subprocesses so one long read cannot starve every quick query |
| **EOS fitting** — Rose–Vinet, Birch–Murnaghan over a range of lattice constants, via ase/pymatgen | Belongs with automated supercell generation, which does not exist yet |
| **Spinels and chalcopyrites** (CuIn₂S₄, CdIn₂Se₄) as prototypes | Orders of magnitude more complex bases than the v1 five. Add once the file format has proven itself |
| **SiC polytypes 4H and 6H** | 3C and 2H are covered by zincblende and wurtzite in v1; the rest need their own prototype entries |
| **Integration tests for the Python bridges** | Identified as a real gap. Wanted, but scoped separately |
| **Text editor: Shift+F search, Tab support, jump-to-atom** | Recorded separately; jump-to-atom needs the atom-index → line-number map, which the POSCAR writer should produce when it lands |
| **Generic numeric bond-length scaling** | Ambiguous for three or more species; the per-prototype formula covers every v1 case exactly |
| **A real sub-viewport split, and `const GetWindows()`** | Both wait on the `RendererWindowState` split in the remediation plan |

---

## Proposed delivery split

Named by what each piece does. **This split is a proposal, not yet agreed.**

**Finish supercell generation** — close out the current branch as it stands. The remediation plan is
waiting on it.

**Wizard usability and live preview.** Periodic table sizing; lattice parameter and basis table
layout; species placeholder; full centering names; n,k,l with live supercell preview; the ephemeral
preview window; the docked split; `Preview basis only` as a checkbox; the primitive-cell overlay;
the always-on lattice point group and the debounced space group; the gizmo writing back to fields
with the pivot and axis enumerations.

**Prototypes and materials.** The two YAML files and their loader; the five v1 prototypes; lattice
constants per functional; formula parsing with the editable site table; bidirectional bond length.
*Worth doing before the wizard usability work if it is wanted soon* — the prototype list changes what
the lattice section draws at all, and doing that layout twice would be waste.

**Structure and text persistence.** POSCAR write through Python with atomic replace; the dirty flag
and its marker; `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+Alt+S`; atom ordering with the defect-pattern key;
the POTCAR checkbox.

**Undo consolidation.** One `UndoStack` class in two instances; the pinned-label history folded into
the per-window stack; the hand-rolled vectors deleted. Overlaps the remediation plan's first step,
which removes the dead `SetCameraViewCommand` and writes the per-window policy into ADR-011 — these
two should be done together or in immediate sequence.

**Open question:** do these run on the current branch, or do we close supercell generation, run the
remediation plan's cheap first stage, and start the wizard work on a fresh branch after that?
