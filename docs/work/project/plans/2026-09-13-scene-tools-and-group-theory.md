# Plan: scene tools (unified transform, Empty, curves, planes, presets) + group-theory panel
_Locked via grill — by Claude + user, 2026-09-13_

Supersedes workstreams 3 (planes part) and 4 of
`visualization-and-group-theory/README.md` and pulls the UI half of workstream 8 forward.
Workstreams 5, 7, 9 are pulled in as tasks 26-28; 6, 10, 11 stay later.

## Goal

Give Defect Studio Blender-grade tools for building publication figures of defects — one unified
G/R/S transform system with axis/plane locks and increment snapping, an Empty helper object,
editable Bézier curves with TikZ-style arrowheads, planes with fast alignment/scaling/rotation, and
shared style presets — all persisted in the project and undoable on one stack. In parallel, ship a
point-group analysis panel quickly: selected atoms as a permutation basis, manual or detected point
group, character table, decomposition, projected vectors, multiplets, copy as Markdown/LaTeX.

## Approach

Integration branch: **`dev`** (new, from `main`). Every task merges into `dev` after a green
Release build + tests; `dev` → `main` later as one batch after the full Debug + Release verify.

0. **`dev` setup.** Create `dev` from `main`. Merge `task/22-process-tree-cleanup` (after manual
   zombie-process check) and `task/19-groupy-bridge-spike` (after Debug build).
1. **`task/20-scene-object-model`** — the real work is the **uncommitted diff in the sibling
   worktree `ds-task20`** (14 files + new `SceneRegistry.cpp`), on top of contract commit `347b9c6`.
   Rebase the branch onto `dev`, reconcile that diff, fix inconsistent bounds checks on
   `AnnotationIndex` results, fix `SceneRegistry::CreateObject(existingId)` so it advances
   `m_NextObjectId` past any explicit id and rejects duplicates, regenerate projects, build, pass
   `SceneObjectModelTests` (+ a test for the explicit-id allocator case), commit, merge. Update README
   workstream status.
2. **`task/23-group-theory-panel`** — PARALLEL TRACK, starts right after step 0 (does not use
   `SceneObjectId`). Candidate for Codex in its own worktree. Shared-file owner rule: the only
   shared touch is panel registration in App/Presentation; task 20 merges first and task 23 rebases
   onto `dev` before its merge and owns resolving that conflict.
   - **Contract first** (same pattern as the spike): `Domain/Symmetry/PointGroupAnalysis.hpp`
     extended with request/result structs for character table (classes, class sizes, irrep rows,
     exact+numeric characters), reducible characters, multiplets (term label, 2S+1, orbital irrep,
     degeneracy); JSON schema documented in the task file; one positive test per result family plus
     negative tests that are verified to fail at the guard they target (not at `import groupy`).
   - **Runtime:** `groupy` is the user's own local package, same status as `punktukas-tools`
     (neither is in `pyproject.toml`; both are installed into `.venv` from a local checkout). Keep
     that convention rather than a non-reproducible local-path entry: add `symengine` to
     `pyproject.toml` `scientific-core`; document the groupy setup prerequisite (source path, install
     command, then `scripts/python/prepare_app_python_runtime.py`) in the task file and README
     setup section; add a smoke test that imports groupy through `ScriptRunner` and FAILS (not
     skips) with a message naming the setup step. No CI exists today, so no CI change.
   - **Multiplets contract check first:** groupy API is `ActiveSpace.from_orbitals(pg, ["A1", "E"],
     nel=4)` → `term_table()` / `terms(irrep, S=...)`. Its input is a list of **orbital irreps +
     electron count**, not the site basis — the panel feeds it the irreps the user marks active
     from the decomposition. A contract test for C₃ᵥ, `[A1, E]`, nel=4 expecting ³A₂, ³E, ¹E, ¹A₁
     (plus whatever else `term_table` returns, asserted exactly) lands before panel work.
     Multiplets are a **hard acceptance criterion** of task 23 (API existence verified at
     `groupy/multiplets/terms.py:192`); term labels, 2S+1 and degeneracy serialize as plain
     strings/ints, state vectors are not part of this task.
   - Basis: "Use selection as basis" → named sites (editable labels) = **exactly the selected
     atoms** (no automatic neighbour shell). Periodic structure: validate the lattice is invertible
     (else `StructuredError`), then unwrap by minimum image around the chosen centre. Non-periodic
     structure: plain Cartesian, no unwrapping. Centre = selection centroid, 3D cursor, or an atom.
     Analysis results are **live-session only** (not persisted in this plan; reopening the project
     means recomputing). Within the session the result carries its full provenance: centre, frame, tolerances, periodic/
     non-periodic branch, resolved site list (index, element, centred position) and its hash.
   - Group: manual pick from list, or "Detect": pymatgen `PointGroupAnalyzer` on the centred
     selected-atom cluster with tolerance — works for defect centres between atoms (NV⁻ vacancy),
     unlike spglib `site_symmetry_symbols` which only describes existing crystal sites. Explicit
     `Undetermined` result with reason. Provenance shown (detected + tolerance / manual).
     Detection never silently overrides: a detected group lower than the user's last manual choice
     is shown as a warning with "try tolerance X". Test fixtures: ideal NV⁻ cluster → C₃ᵥ;
     slightly relaxed NV⁻ (atoms perturbed ≤ tolerance) → C₃ᵥ; distorted beyond tolerance → lower
     group reported (Cₛ or C₁), not an error.
   - Bridge extension, one batched subprocess per request: character table, reducible characters,
     decomposition, projected vectors (exact string + numeric), `ActiveSpace` multiplets
     (term label, spin, degeneracy) for a given electron count.
   - Concurrency: each request carries a monotonically increasing revision token plus the
     structure revision and basis hash; the job completion is delivered to the main thread (existing
     JobSystem completion path / EventBus), and the panel commits only if token and revisions still
     match, otherwise discards and keeps the "stale" marker.
   - Errors: the job preserves `StructuredError` (category/code/details) instead of converting to
     `std::runtime_error` text (current pattern in `GetSymmetryInfoJob.cpp`); panel distinguishes
     groupy missing / invalid or non-closed basis / undetermined group / malformed output /
     cancelled.
   - Tables: character table; `Γ = 2A₁ ⊕ E` with reducible characters; projected vectors keyed by
     `(irrepLabel, occurrenceIndex, irrepRow)`, exact shown, numeric in tooltip; multiplets.
   - Physical names (`a₁'`, `eₓ`…) only as manual, visibly-marked assumptions.
   - Copy-as-Markdown / copy-as-LaTeX per table; Unicode super/subscripts, no LaTeX parser.
   - Stale marker + "Recompute" when basis or structure changes.
   - Runs through a job, never on the main thread.
3. **`task/24-scene-object-persistence`** — `scene_objects.yaml` next to the project manifest.
   - **Ownership:** scene objects are stored **per structure entry** (keyed by the project's
     structure record key), not per window. `SceneObjectId` stays per-window runtime identity: ids
     in the file are file-local, remapped to freshly allocated ids on load; intra-file references
     (future: preset refs, Active Empty, parent) are remapped through the same table.
   - **Schema:** `formatVersion: 1`; `structures: [{structureKey, objects: [...]}]`; each object is
     a tagged entry `kind:` + per-kind payload with required fields and defaults:
     `PinnedMeasurement` (atomRefs `[{index, element, position}]`, `frozenAnchor` positions,
     `linkBroken`, label offset, style), `FreeLabel` (text, position, rotation, style), `SceneArrow`
     (kind Line/Arrow2D/Arrow3D, orientation2D, fixedPlane, start, end, style).
     Unknown kinds and invalid entries are **skipped with a `StructuredError` warning** and not
     preserved on re-save (single-user app; a newer file opened in an older build is not a supported
     workflow). Never abort the project load.
   - **Atom references:** there is no stable atom id in the domain today. Store index + element +
     Cartesian position + `frozenAnchor`. On load a reference binds only if the index exists, the
     element matches and the atom is within tolerance of the stored position (so a reorder that
     moves a same-element atom into the index still fails the position check); otherwise the object
     renders from `frozenAnchor`, `linkBroken = true`, and Properties shows it. `ponytail:` stable
     atom ids in `CrystalStructure` are the upgrade path, deferred until links break in practice.
   - **Dirty tracking:** every persisted scene mutation (the same sites that push
     `SceneObjectsSnapshotCommand`, and undo/redo of them) bumps the owning `StructureRecord::revision`
     (`Domain/ProjectWorkspace.hpp`), so the existing unsaved-changes prompt covers scene objects;
     `revision` only ever increases (undo/redo bump it too, so undo-after-save is dirty);
     `savedRevision` is set to `revision` only after BOTH `scene_objects.yaml` and the manifest
     were written successfully; any failure leaves it unchanged. Each file is written temp +
     rename, `scene_objects.yaml` first, manifest second. No two-file transaction: the manifest does not reference `scene_objects.yaml` and
     neither file's validity depends on the other (a missing or older scene file just loads fewer
     or older objects, a missing one loads none), so a crash between the two renames leaves a
     loadable project, and the unchanged `savedRevision` keeps it dirty.
   - `IO` only (de)serializes plain data; mapping to `RendererWindowState` lives in Renderer.
   - **Multiple windows on one structure:** opening the same file twice creates a second window
     (`RendererLayer.cpp:339-354`); whether both share a `StructureId` is verified first in this
     task. If they can share: every persisted object carries a `persistKey` (random 128-bit hex,
     assigned at creation, kept at runtime next to the per-window `SceneObjectId`, copied as-is
     when two windows load the same file). Save writes the union of those windows' objects
     **deduplicated by `persistKey`**; for the same key the most recently focused window's version
     wins; objects deleted in one window but present in another survive (warning shown). Unchanged
     objects opened in two windows therefore never duplicate. Reload gives every window the merged
     set. Undo commands target a window id, never a structure, so they
     cannot apply to the wrong runtime copy. `ponytail:` one canonical scene per structure shared by
     all its windows is the upgrade path (listed under Later).
   - Tests: round-trip per kind, unknown kind skipped with a warning and absent after re-save,
     broken-link detection, failed save leaves the project dirty, undo after save makes it dirty. Every later task
     adds its own kind + round-trip test and bumps `formatVersion` only when an existing kind
     changes shape.
4. **`task/25-unified-transform`** — one modal G/R/S system replacing the three copies
   (`ViewportGizmo.cpp`, `ViewportLabelGizmo.cpp`, `ViewportSceneArrowGizmo.cpp`).
   - Shared: key/mouse handling, constraint state, numeric entry, cancel (Esc/RMB), axis-line
     drawing, header readout. Per-kind adapter: get/set position/rotation/scale, pivot contribution,
     undo snapshot.
   - Constraints: `X/Y/Z` cycles world axis → local axis → none; `Shift+X/Y/Z` same for planes.
     Keys are always X/Y/Z; the active orientation decides meaning (Lattice: X=a, Y=b, Z=c;
     `Shift+X` under Lattice moves along b and c, not perpendicular to a).
   - Snapping: hold `Ctrl` → increments (G 0.1 Å, R 5°, S 0.1); `Shift+Ctrl` → 10× finer; snaps the
     delta from start, not absolute grid. Steps editable in Settings → Viewport, shown in header.
   - Toolbar: `Transform Orientation = Global | Local | Lattice | Active Empty` and
     `Pivot = Median | 3D Cursor | Active Empty | Individual Origins` (Individual greyed for atoms).
     Replaces `ArrowGizmoPivotMode`. `Active Empty` options are present but disabled until task 29.
   - Undo unification: the existing app-global `CoreLayer` `UndoStack` (already shared by atom
     commands). Scene objects use one generic `SceneObjectsSnapshotCommand` holding the target
     window id + before/after payloads of changed objects by `SceneObjectId`; it resolves
     its window at execute time like atom commands do; if the window is gone it returns a
     `StructuredError` ("target unavailable") from `Undo`/`Redo` (`UndoStack` already returns
     `Result<void>`) so the undo index does not move and the user sees why.
     Covers every scene mutation: add, delete, transform, style edit, align, preset apply. Atoms keep
     their commands; `pinnedMeasurementUndoHistory`/`RedoHistory` and their shortcut are removed
     only after every mutation site is migrated (grep-checked). One drag / one modal op = one entry;
     Properties field = one entry per commit. No save-clean tracking added (none exists today).
   - Migrate atoms, labels, arrows onto it; atoms keep their undo commands behind the adapter.
5. **`task/26-bonds-and-orbitals`** (old workstream 5, pulled forward by user decision
   2026-09-13, option a) — scene-owned objects, G/R/S via the unified transform, undo, persistence.
   - Bonds: standalone or anchored to two atoms (same atom-ref binding as task 24), abstract
     type/order, render single/double/triple/dashed; no chemistry inferred from distances.
   - Procedural orbitals: optional atom/atom-group link, type, orientation, scale, phase, color.
     Presets `s, p, d, sp, sp², sp³, σ, σ*, π, π*, δ, δ*`; global shape controls + per-lobe
     elongation/width overrides (NV⁻ dangling-bond lobes). Reuse `IsosurfaceMesher` / existing mesh
     path. Illustrative only — never claimed to be a normalized wavefunction.
6. **`task/27-basis-objects`** (old workstream 7) — named, saved basis = ordered components
   (atom sites, orbitals, bond orbitals, later electronic states); "Add to basis" from Properties;
   basis panel (order, remove, select, highlight in scene). Coefficients/metadata separate from
   style. Group-theory panel v1 input "Use selection as basis" becomes one way to fill a basis.
7. **`task/28-group-theory-panel-v2`** — orbital bases: operation → component transformation
   matrices for p/d orbitals (not just site permutation); projected orbitals (a₁', a₁, eₓ, e_y)
   rendered as orbital objects linked to the basis; full NV⁻ acceptance with visuals (old
   workstream 9). WAVECAR states stay later.
8. **`task/29-empty`** — renderer/scene-owned (never in `CrystalStructure`/POSCAR).
   Data: id, name, position, orientation (quat), display size, shape
   (`Plain Axes | Arrows (default, RGB + XYZ letters) | Cube | Sphere`), unused optional `parent`.
   `Shift+A → Empty` at 3D cursor. G/R via unified transform; S = display size only.
   Align: Z to selected atoms (2 atoms: along segment; ≥3: best-fit plane normal), to world, to
   view, position to selection centre. Enables `Active Empty` pivot/orientation. Outliner row,
   undo, persistence.
9. **`task/30-bezier-curves`** — replaces `SceneArrow` (straight arrow = 2 points, `Vector`
   handles). Persistence: new `Curve` kind, `formatVersion: 2`; loader maps v1 `SceneArrow`
   entries (kind, orientation2D, fixedPlane, start, end, style incl. head width/length → `Cone` or
   flat head) to 2-point curves; saving always writes v2. Test: a checked-in v1 fixture loads into
   the expected curve.
   - Degenerate cases: coincident control points / zero-length segments are skipped when sampling
     (a curve with total length ≈ 0 renders as nothing and shows a Properties warning); head length
     is clamped so start+end heads never exceed 90% of curve length; closed curves correct the
     parallel-transport twist at the seam by distributing the residual angle along the length.
   - Geometry: ordered control points with left/right handles, cubic segments, open/closed.
     Handle types `Auto | Aligned | Vector | Free` (`V` menu).
   - Object mode: G/R/S whole curve. Edit mode (`Tab`): select points/handles, G/R/S with all
     constraints/snapping, `E` extrude, `Ctrl+RMB` add at cursor, `X/Delete` remove,
     `W → Subdivide`, `Alt+C` toggle closed, `Alt+S` per-point radius (taper).
   - `Shift+A → Curve → Bézier | Straight Arrow` at 3D cursor. 2D curves keep
     `Billboard | FixedPlane`.
   - Arrowheads: independent start / end, optional mid. Types `None, Stealth (default), Latex,
     Triangle (filled/open), Open, Cone (3D), Harpoon (left/right), Bar, Circle, Square, Diamond`
     (filled/open where applicable). Oriented by curve tangent; size as multiple of line width
     (switchable to absolute Å); shaft trimmed at head base; flat heads lie in curve plane or
     billboard, `Cone` is the only solid head.
   - Rendering 3D: tube swept along adaptively sampled polyline with parallel-transport frames,
     generalizing `BuildWeldedArrowMesh`; mesh cached, rebuilt only on geometry/style edit,
     transform-only changes reuse it; `Cone` welded at ends.
   - Rendering 2D/Line: screen-space thick polyline (vertex-shader-expanded strip, round joins),
     dashes by arc length in fragment shader; flat heads via `arrow_quad.frag` SDF extended per
     head type.
   - Edit-mode overlay (points, handles, handle lines) via ImGui drawlist; picking by screen-space
     distance to projected polyline. F12: px widths scale with render resolution.
   - Style: color/alpha, width (Å|px), outline, pattern (`Solid|Dashed|Dotted|Dash-Dot|Custom`
     dash/gap/offset), caps (`Butt|Round`), heads, per-point radius, sampling resolution.
   - Color mode `Solid | Gradient`: stops `(t, color, alpha)` by arc length, ColorRamp-style
     editor; heads take the end color on their side. Curves only.
10. **`task/31-planes`**
   - Local frame: centre, in-plane U/V (parallel to edges), normal W; size U×V.
   - Align: `XY | YZ | XZ` (plane lies in it, normal along the third axis; keeps centre + size),
     `Lattice (hkl)`, `View`, `Active Empty`, `Selected atoms` (3 atoms exact, more best-fit).
     These replace separate point+normal / three-point / Miller construction modes.
   - Scale: `S` uniform U/V; `S X`/`S Y` local U or V; `S Z` disabled; `S Shift+Z` = uniform U/V;
     `S Shift+X` = V only. Ctrl snapping comes free from unified transform.
   - Rotate: `R X`/`R Y` about local U/V (edge bisectors through centre, default); `R Z` in-plane
     about normal; "Rotate around edge" (hinge; edge picked in edit mode or Properties); angle field.
   - Display: `Finite | Clipped to cell | Infinite`, fill color/alpha, outline (color, width,
     curve line patterns), optional in-plane grid.
   - Degenerate cases: `Selected atoms` with <3 distinct points, collinear points (smallest
     singular value below tolerance) or duplicates → operation refused with a status-bar message,
     plane unchanged; `Lattice (0 0 0)` refused; scale clamped to a minimum size; `Clipped to cell`
     with no intersection renders nothing and warns in Properties. Empty "Align Z to atoms" uses the
     same guards (2 coincident atoms or ≥3 collinear → refused).
11. **`task/32-style-presets`** — per object kind (curve, plane, Empty, label).
   Object = preset reference + local overrides; un-overridden fields follow the preset; override
   marker + "reset to preset" in Properties. Save as / Apply to selection / rename / delete
   (delete freezes values as overrides). Built-in read-only presets: `Symmetry axis`,
   `Displacement vector`, `Spin up/down`, `Dimension`, `Mirror plane σᵥ`. User presets in
   `config/`; used presets copied into the project YAML on save; project copy wins on name clash
   inside that project.
12. **Later (end of plan):** Empty parenting (children follow parent transform, outliner tree,
   persistence); path animation; symmetry-element overlays generated from the group-theory panel
   (needs 30 + 31); WAVECAR bases (old workstream 6), full Markdown/LaTeX renderer (old
   workstream 10).

Order: 0 → 1 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11; 2 parallel after 0. Bonds/orbitals, basis
objects and panel v2 come before Empty/curves/planes/presets (user decision 2026-09-13).

## Key decisions & tradeoffs

- **`dev` integration branch** instead of merging each task straight to `main`.
- **Unify the transform modal before adding features** — costs a refactor task and regression risk
  on atoms, but `Shift+XYZ` / `Ctrl` snapping get written once instead of five times. Atoms keep
  their existing undo commands behind the adapter.
- **X/Y/Z semantics fixed, orientation changes their meaning** (no A/B/C keys — avoids clashing with
  `Shift+A` add menu; modal also captures keyboard).
- **Snap the delta, not absolute grid**; Cartesian steps only, fractional-lattice snapping skipped.
- **Single curve object with heads as style**, not separate path + arrow objects (old plan) — no
  current consumer for the split.
- **Curves replace `SceneArrow`** with load-time migration rather than living alongside it.
- **Planes: alignment operations replace construction modes.**
- **Presets pulled into scope now** (previously deferred), with preset+override semantics.
- **Group-theory panel as parallel track**, not queued behind visuals — user needs it soon; file
  sets are disjoint.
- **Persistence as its own early task** instead of "renderer-local first, persistence last" —
  unsaved figures have little value and presets need project storage anyway.
- **One undo stack** — fixes cross-kind Ctrl+Z ordering; removes the label-specific undo shortcut.
- **Point-group detection via pymatgen `PointGroupAnalyzer` on a centred cluster** (already an
  installed dependency), not spglib site symmetry (fails for centres between atoms) and not a new
  detector.
- **Persist per structure entry with file-local ids remapped on load**, not per window and not
  global persistent `SceneObjectId`s.
- **No stable atom ids yet** — index+element+position validation with a visible "link broken" state.

## Risks / open questions

- Unified transform regression on atoms (ImGuizmo path, existing `ICommand` undo) — needs
  behaviour-preserving tests before migration.
- Cluster point-group detection is tolerance-sensitive on relaxed defect geometries (slightly
  distorted C₃ᵥ may read as Cₛ/C₁) — tolerance is user-editable and shown with the result.
- Atom references break after atom add/delete/reorder until stable atom ids exist; mitigated by
  validation + "link broken" state, not solved.
- Screen-space dash/width consistency between viewport and F12 offscreen render.
- `task/20` and `task/23` touch panel registration in App concurrently — small merge conflict
  expected.
- Merging `dev` → `main` as a batch makes the final verify larger.

## Out of scope

- Groups, collections, Origin, Light (Settings-controlled), F12, isosurface — already done or
  dropped.
- Fractional-coordinate snapping; absolute-grid snapping.
- Surface gradients on planes.
- Empty parenting and path animation (listed under "Later").
- Persisting group-theory analysis results/provenance across sessions; one canonical scene shared
  by several windows of the same structure.
- WAVECAR bases, full Markdown/LaTeX parser, symmetry-element scene overlays — later tasks.
- FERWE/FERDO, diffusion path generation, space-group/k-point analysis.
