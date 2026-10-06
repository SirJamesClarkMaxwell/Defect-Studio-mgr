# PathSystem redesign — v2 (reconciled plan)

Status: **reconciled Claude + Codex plan (Codex round 2 = REVISE, all 10 edits applied, no remaining disagreement); user decisions closed 2026-09-20; ready to start branch 1**
Date: 2026-09-20
Supersedes: `2026-09-20-path-system-redesign.md` (kept for history; where the two differ, this file wins)
Inputs: original plan, `defect_studio_blender_inspirations_and_roadmap.md`, review rounds in the session
scratchpad (Claude findings F1–F16, Codex adjudication + additions).

Goal, replacement boundary, single geometry contract, and the "no runtime adapters for SceneArrow"
rule are unchanged from the original. This file records only what changed and why.

## 1. Corrections to the original design

| # | Original | Corrected | Why (repo evidence) |
|---|----------|-----------|---------------------|
| C1 | Path commands on `Core/Undo`, "do not join the legacy snapshot" | `PathStore` is a copyable value type; its slice **joins `SceneObjectsSnapshot`** for coarse mixed ops (delete, paste, duplicate, mixed G/R/S). Fine-grained path-only edits (MoveNode, SetHandleType, …) are separate commands on the same stack, grouped with `UndoScope`. | `SceneObjectsSnapshotCommand.cpp:97` snapshots `sceneArrows` with labels/orbitals; `ViewportModalTransform.cpp:217` groups atom + scene-object transforms in one scope. A store outside the snapshot silently drops paths on mixed undo. |
| C2 | Arc = analytic `center/normal/radius/startAngle/sweep`, endpoints also stored as nodes | **Nodes are the only source of truth.** `ArcSegment { planeNormal; signedSweepRadians; }`. Derived: chord, `r = \|AB\| / (2 sin(\|θ\|/2))`, center, start angle. | Two arcs sharing a node would each own that endpoint. Validation: nonzero chord, finite normal, normal not parallel to chord (re-orthogonalise against AB), `ε ≤ \|θ\| ≤ 2π − ε`, finite derived values. Full circle stays out of V1 (cyclic is deferred). |
| C3 | "Mandatory tangent continuity across mixed types" | **C0 is the only invariant.** G1 is the operation `Make Tangent` (available when ≥1 neighbour is Cubic). | Line–Arc, Arc–Arc, Line–Line with rigid geometry are legal corners (`SceneArrowGeometry.cpp:222` today only special-cases one quadratic). |
| C4 | `PathSystem` "not in RendererWindowState" | **One member `Unique<PathSystem>` in `RendererWindowState`.** `PathStore` copyable; caches non-copyable, rebuilt. Object-level selection stays in the existing unified per-window selection; `PathEditSession` owns only element-level `(SceneObjectId, PathElementId)` selection and edit-mode state. | `RendererLayer::m_Windows` is a by-value vector (`RendererLayer.cpp:367`); export builds a copied `previewState` (`:587`); mixed G/R/S relies on the shared selection (`RendererWindowState.hpp:359`). |
| C5 | Stage 8 `PathInstanceSet` + displacement cutover | **Cut.** Displacement arrows stay on their existing instanced pass. Only share the decoration/tip vocabulary. | `OpenGlRendererBackend.cpp:1961/2121` already batches shaft + head instances. No second consumer yet (roadmap §17 shares arrowhead *style*, not machinery). Revisit when a real second consumer exists. This is a YAGNI decision, not a claim that single-draw displacement rendering is technically impossible. Displacement data and rendering remain unchanged; at most, neutral tip constants may be shared. |
| C6 | Cache: global `geometryRevision` + camera in mesh key | Analytic evaluation is camera-independent. Tessellated centreline/sample caches are keyed by **per-path geometry revision + resolved-binding/source revision + quantised LOD bucket with hysteresis** — not the raw camera. Round world-space meshes may be CPU-generated; CameraFacing orientation and ScreenPixels width are expanded in the **vertex shader**. Style/material revisions stay separate. Export scale for ScreenPixels = `targetHeight / sourceViewportHeight`. Evict all layers on path delete, window close, GL shutdown, preview-state destruction. | Perspective + constant pixel width ⇒ world width varies per vertex; `arrow_quad.vert:20` today only consumes CPU-computed basis vectors, so the shader work is new. |
| C7 | Decoration = shared semantic definition | Decoration = **axial contour + fill/stroke-only flag + back-closure flag + trim distance**. Round revolves a filled contour; Flat triangulates it in the ribbon plane. OpenArrow = two strokes; Round OpenArrow is not built in V1: the fallback is a planar two-stroke decoration oriented by the evaluated endpoint frame, never silently filled; rendering, picking and export use that same geometry. | `SceneArrowGeometry.hpp:12` already carries `filled`/`closesBack`; a scalar `w(s)` cannot express OpenArrow/Bar. |
| C8 | Bindings resolved "before evaluation" | Separate **authored** data (incl. frozen fallback position), **resolved** positions, **evaluated** products. Refresh never rewrites serialized authored data. `buffer` allowed **only on endpoint `CopyPosition`**, offset toward the sole neighbour's *unbuffered* position, existing non-inversion clamp applied (`SceneSystem.cpp:328`). Interior bindings: `offset` only. `ObjectOrigin` may **not** target a ScenePath in V1 (no origin model, avoids cycles). | Current resolver mutates stored endpoints (`ViewportInteraction.cpp:30`); interior buffer has two neighbours ⇒ undefined direction. |
| C9 | Precision open | Store `float`; evaluate arcs, arc length, adaptive error, interpolation in `double`; convert to `float` only at the mesh boundary; persist with `max_digits10`. "Exact 120°" = stable scalar round-trip + tolerance-based validation. | Whole scene is `glm::vec3` (`RendererWindowState.hpp:207`). |
| C10 | Importer "IO-only" | **IO parses v1/v2 DTOs only.** v1-arrow → ScenePath conversion lives in `Renderer/Scene` next to `SceneObjectPersistence.cpp:263`. Unsupported future version rejected before interpretation. First migrating save writes a non-overwriting `scene_objects.yaml.v1.bak`; **backup failure aborts the save** with a `StructuredError`. | `SceneObjectsIO.hpp:16` states the IO/renderer boundary. Thesis data deserves the safeguard. |
| C11 | Path element IDs unspecified | Monotonic, non-reused **per path**; preserved by snapshots/undo; **regenerated on load** (allowed only because nothing outside the path can reference an element in V1 — see C8 `ObjectOrigin` rule). | Mirrors `SceneObjectId` contract (`SceneObject.hpp:24`). |
| C12 | Commands/shortcuts | All user-visible path operations go through `CommandRegistry` + `CommandService` + keymap. After mutation publish the existing scene-object-modified event (`RendererLayer.cpp:52` → `EditorLayer.cpp:774`) so project dirty tracking works. | Raw shortcut handling in `ViewportLabelInteraction.cpp:153` is precedent to remove, not copy. |
| C13 | GL harness "Windows + Linux hard contract" | Platform-neutral harness code. **Windows is the canonical hard gate**: missing required capabilities fail there. Any capable Linux environment runs the structural pixel-buffer assertions; only genuinely missing capabilities produce an explicit skip. Primary assertions are **structural** (bbox, coverage, anchor-pixel colours, thickness, depth ordering) on the pixel buffer directly; PNG goldens are secondary and run only when the recorded `GL_VENDOR`/`GL_RENDERER`/`GL_VERSION` match the baseline. Harness loads the real shader/primitive bundle (`OpenGlRendererBackend.cpp:706`) — the test target needs the shader-copy step. Extract readback + vertical flip into a helper shared with the PNG exporter (`:3443`). Graphics tests serialised. | Linux configs exist (`premake5.lua:588,749`); no CI exists; driver drift is the real risk. |
| C14 | Reuse | Lift the tip table (`GetArrowTipParameters`, `SceneArrowGeometry.hpp:25`) and the arc-length dash-interval logic (`SceneArrowGeometry.cpp:255`) into `Renderer/Path` behind the legacy wrappers; delete wrappers at removal. **Do not** reuse the fixed quadratic tessellator as the new evaluator. | AGENTS.md reuse-first; roadmap §4.3. |

## 2. V1 scope changes that need explicit user sign-off

The original plan says locked behaviour may only change with a call-out. These are the changes:

| Cut from V1 | Reason | Consequence for the user |
|-------------|--------|--------------------------|
| **XRay** depth mode | Render-pass contract undefined (checklist #14); no `DepthMode` type exists today. User: paths are ordinary scene objects, XRay not needed. | Keep `DepthTest` (default, occluded like any object) + `AlwaysOnTop` (needed to preserve migrated Arrow2D, which today draws in a depth-disabled late pass). |
| **ScreenPixels for dash/gap** | Two more unit conversions + shader work for little value. | Dashes are world-space; stroke *width* stays ScreenPixels-capable. |
| **Miter** join | Round + Bevel cover the figures. | — |
| **Outline** (outlineColor/outlineWidth) | Decided by user 2026-09-20: not a per-arrow feature. It returns as a **global, system-level outline** during the full-ECS migration (section 9), where it also enables correct object selection highlighting. | Regression vs today's Arrow2D obwodka until then. Migration warns when a v1 outline is nonzero. |
| **Triangle** decoration | Duplicate of legacy `Plain` (already a filled, back-closed triangle). | Not added. |
| ~~Square, Diamond~~ | **Reinstated in V1 at user request.** Cheap once decorations are contours (C7): Square = rectangle contour, Diamond = rhombus contour; Round revolves (cylinder / bicone), Flat triangulates. | V1 decorations = the six legacy tips + Square + Diamond = eight. Legacy mapping unchanged: Plain→Arrow, Barbed→Stealth, Open→OpenArrow, Bar→Bar, Circle→Circle, None→None. Cost per new kind: YAML name, UI entry, Flat + Round test. |
| **PathInstanceSet / displacement migration** | C5. Decided by user 2026-09-20: displacement is not touched now. At the very end, once everything exists, only the objects inside the relevant draw method (`renderDisplacementArrows`) are swapped for the new path objects; no new instancing mechanism. | Old plan Stage 8 disappears; displacement behaves exactly as today until that final swap. |
| **Figure Shots / batch export** | PNG export exists; symmetry annotations are the next real path consumer. | Later, after the symmetry generator. |

Confirmed by user 2026-09-20: XRay, ScreenPixels dash, Miter, Figure Shots cut/deferred; Square + Diamond added. Outline and displacement decided the same day (see rows below).

Everything else in original §2.1 stays: Line/Cubic/Arc, four handle types, Round/Flat, three
orientations, Bevel/Round joins, three caps, gradient over arc length, independent start/end
decorations, DepthTest/AlwaysOnTop, Object + multi-object Edit Mode, bindings, YAML v2 + v1 import,
PNG export, automated tests.

## 3. Topology contract (added; original had none)

- **Insert** into Line: exact split. Cubic: exact de Casteljau split. Arc: sweep split `tθ` / `(1−t)θ`, same plane.
- **Extend** (`E`): new segment inherits the terminal segment's type; Cubic gets Vector handles; Arc inherits normal and sweep as editable initial values.
- **Delete** interior node: both neighbours Line → merge to one Line; otherwise **reject/skip** with `StructuredError` (V1 does not silently refit). Delete endpoint → removes its adjacent segment. One remaining node = serialisable, non-renderable degenerate path. Deleting the last node is rejected (`DeletePath` removes the object).
- **Reverse**: reverse node + segment order; swap cubic handles; negate arc sweeps; swap start/end decorations; map gradient stops `p → 1−p`; adjust dash phase so the pattern does not jump.
- **Handle rules** (deterministic): Cubic–Cubic: opposite rays along `normalize(next − prev)`, handle length = ⅓ adjacent chord. Cubic–Line/Arc: cubic handle aligned to the rigid neighbour's end tangent. Open ends point inward. Degenerate ⇒ `StructuredError`, no invented direction. Auto never changes a rigid segment's type.
- **Multi-path commands**: validate the whole target set first; fatal validation changes nothing; permitted skips are reported; one invocation = one `UndoScope`.
- **Arc edits** (numeric center/axis/radius/start/sweep) run through one atomic solver → new endpoint positions → one command; either one valid result or rejection.

## 4. Migration mapping (v1 → ScenePath), tightened

- Quadratic control point (two-point paths only) → cubic: `C1 = P0 + ⅔(Q−P0)`, `C2 = P2 + ⅔(Q−P2)`. Never applied to longer paths.
- N points → N−1 Line segments. `curveSegments` → dropped (adaptive), reported.
- Arrow2D Billboard → Flat + CameraFacing + ScreenPixels width; FixedPlane → Flat + FixedNormal(plane normal) + ScreenPixels; Arrow3D/Line → Round + WorldUnits.
- Tips per §2 mapping. Two-stop gradient → two stops. `alpha` → global alpha. `dashed/dash/gap` → world-space pattern.
- Atom anchors + `atomBuffer` → endpoint `CopyPosition` binding with buffer (C8).
- Non-zero outline → warning naming the loss (unless outline is included per §2).
- Invalid-but-finite geometry retained with diagnostic. No legacy `SceneArrow` reaches runtime after import.

## 5. Delivery — 16 stages on ONE branch (`task/41-path-system`)

> Decided by the user 2026-09-20: the work is linear, so the 16 "branches" below are **stages/commits on a single branch**. Per-stage gates still apply; see `2026-09-20-path-system-implementation.md` for the file-level plan. The `task/NN` names in the table are stage labels only.

Each branch ends green on Release build + tests; user-visible ones also pass their manual subset.
Old SceneArrow stays the running application until branch 15. **Open the PR for `task/40` first**:
users live with the old arrow until the cutover. (Branches 12 and 13 may be merged at execution time
if the overlay work turns out small; they are split here because they are independently reviewable.)

| # | Branch | Content | Exit gate |
|---|--------|---------|-----------|
| 1 | `task/41-path-core` | Reuse lift (C14); IDs; node/segment model; Line/Cubic/endpoint-Arc + validation; float storage/double eval; pure authored→resolved binding resolver | Legacy helper tests unchanged; analytic + invalid-arc tests; resolver never mutates authored data; `max_digits10` scalar round-trip (120°) |
| 2 | `task/42-path-topology` | §3 topology, handle rules, Make Tangent | Exact splits, reverse involution, atomic rejection |
| 3 | `task/43-path-tessellation` | Adaptive subdivision, bounds/diagnostics, LOD hysteresis, parallel-transport frames | Bounded error; stable frames; no NaN/Inf |
| 4 | `task/44-gl-test-harness` | C13; independent of path types | Windows structural smoke; capable Linux structural run; capability skips explicit |
| 5 | `task/45-path-stroke-decorations` | Round/Flat mesh, Bevel/Round joins, caps, world dashes, gradient, eight decorations (six legacy + Square + Diamond) + trimming | Deterministic mesh/interval tests; every decoration (incl. Square/Diamond) has defined Flat/Round behaviour |
| 6 | `task/46-path-ownership-cache` | `Unique<PathSystem>`, copyable store, `SceneObjectKind::ScenePath` + registry mirroring, revisions and eviction | Add/remove/move, snapshot copy, export-preview copy, isolated invalidation, teardown |
| 7 | `task/47-path-render-dev` | GL renderer, orientations, shader-side pixels, DepthTest/AlwaysOnTop, non-UI fixture injection behind a non-default dev switch | Structural viewport/export assertions; no legacy adapter; no user-visible raw mutation |
| 8 | `task/48-path-commands-undo` | C1 + C12: registry commands, keymaps, snapshot slice, fine edits, dirty events | Mixed undo restores every store once; no raw path shortcuts |
| 9 | `task/49-path-persistence` | C10 + §4: v2 DTO, migration in Renderer/Scene, future guard, `.v1.bak` | Round trip; every v1 mapping; backup failure blocks save |
| 10 | `task/50-path-picking` | Stroke/decoration/node/handle picking + overlay geometry from the evaluated path | Render/pick parity; hidden-geometry exclusion; arbitration priority |
| 11 | `task/51-path-object-mode` | Creation presets, Outliner/Properties, selection, region select, clipboard/duplicate/delete | Mixed object ops preserve IDs, undo, dirty state |
| 12 | `task/52-path-edit-mode` | Element selection, overlays, drag + G/R/S transactions | One undo item per multi-path edit; cancel restores exact state |
| 13 | `task/53-path-topology-ui` | Extend/delete/insert/handle modes/reverse, numeric arc editor | Automated + manual topology matrix |
| 14 | `task/54-path-bindings-acceptance` | Binding UI/refresh, endpoint buffers, broken-reference fallback, export, saved C3 scene | Acceptance steps 1-13 from original section 14, **as amended by sections 2 and 7** (no XRay, no ScreenPixels-dash, no Triangle/Miter/outline requirement), through the dev route after reopen + export |
| 15 | `task/55-path-cutover` | Default-route flip only | No user-reachable legacy SceneArrow; displacement unchanged |
| 16 | `task/56-path-legacy-removal` | Delete legacy runtime/UI/shaders/tests/wrappers; regenerate projects; update Graphify | Legacy names only in v1 DTO/importer/fixtures; Debug + Release build; full matrix; `architecture-boundary-review` clean |

**Dependencies.** Branch 4 may be developed in parallel with 1-3 (it tests the existing renderer, no
Path dependency) but must merge before branch 7. Ownership/cache (6) precedes commands (8) and object
mode (11). The renderer (7) depends on tessellation (3), stroke/decorations (5), ownership (6) and the
harness (4). Object mode (11) depends on commands (8), persistence (9) and picking (10).

**Cutover honesty.** The SceneArrow footprint is a moving target (56 `src` + 13 test files at the
time of review, not a fixed number). Cutover (15) is routing-only **only if** branch 14 already
supports, behind one non-default dev switch, the complete ScenePath route for: creation, load/save,
rendering, export preview, visibility, picking, region selection, Outliner/Properties, clipboard,
delete/duplicate, mixed G/R/S, Edit Mode, bindings, undo and dirty tracking. Maintain an audited
inventory of every legacy reference, classified compile-time replacement / runtime routing /
serialization / behavioural parity. Branch 15 may change only default/composition routing and remove
legacy reachability; newly discovered ScenePath behaviour returns to its owning earlier branch.
That is a flag, not an adapter.

## 6. Roadmap disposition (`defect_studio_blender_inspirations_and_roadmap.md`)

Sound as an idea bank; its own §47 phase order should change to thesis value:

1. ScenePath subset (this plan) → C3/σ annotations
2. Symmetry annotation generator (next concrete path consumer; ties into the Group Theory panel)
3. Named Figure Shots + generalised batch export
4. Collections / View Layers, SceneObject/Data/Style separation (only as pain appears)
5. Park until ≥2 real consumers: Modifier stack (§8), Geometry Nodes Lite (§36), Asset Browser (§23), Publication Composer (§46), constraint *stack* (§10)

Keep in mind: §29 (SVG/PDF vector overlay) is the strongest argument for keeping `PathEvaluator`
renderer-independent — it already is. `StrokeStyle`/`EndpointDecoration` stay separate value
structs so they can become shared styles; `PathBinding` is shaped to become a Constraint variant.
Roadmap items 41–46 (Measure 2.0, Local View, structure modifier stack, provenance, workspaces)
are independent of this plan.

## 7. Test plan deltas vs original §12–§13

- Structural assertions primary, goldens secondary (C13). Golden updates need the per-test env flag and a matching `GL_RENDERER`.
- Add: arc validation matrix, reverse involution, exact-split geometry preservation, snapshot-slice undo across mixed ops, per-path cache isolation, `.v1.bak` failure path, LOD-bucket hysteresis (no rebuild on sub-bucket zoom), export-scale thickness.
- Remove: XRay, ScreenPixels-dash, Miter, PathInstanceSet/displacement goldens. Acceptance steps referencing them are dropped (see branch 14).

## 8. Open decisions

None. Outline, displacement and roadmap order were settled by the user on 2026-09-20.

## 9. ECS decision (user, 2026-09-20)

**Now:** ScenePath is a normal scene object in behaviour (id, Outliner, selection, visibility, undo,
persistence, clipboard) using today's **mirror model**: data in `PathStore`, `SceneRegistry` mirrors
identity (`SceneObjectComponent`), node positions are world coordinates, no object transform.
**Later, separate project:** full game-engine / Blender-style ECS for *everything* (atoms, bonds, labels,
planes, orbitals, paths): data as components, transform component, parenting. That is roadmap §20
"Scene Architecture v2"; it gets its own plan and grill after this workstream. It is not started here.

ECS-readiness rules for branches 1-16 (cheap now, each one removes a step from the later migration):

1. `ScenePath` is a plain copyable value struct: no pointers/references into `RendererWindowState`, no caches inside it.
2. Everything addresses a path by `SceneObjectId`. Only the mirror-sync layer may read `sourceIndex` for paths.
3. `StrokeStyle`, `EndpointDecoration`, gradient, `PathBinding` stay separate value structs (future style/constraint components).
4. `PathStore` exposes a narrow id-based API (find/insert/erase/visit); no consumer holds a raw pointer across a frame or command. Swapping the storage for components then stays local to `PathStore`.
5. Node positions are world coordinates in V1. At ECS migration they become local coordinates under a `Transform` component with identity transform, so the conversion is lossless and one-way. The V1 restriction "`ObjectOrigin` may not target a ScenePath" (C8) is lifted at that point.
6. No new path code may add a per-kind vector to `RendererWindowState` (already guaranteed by C4: one `Unique<PathSystem>` member).
7. Outline (global, system-level, also drives selection highlighting) is scheduled for the ECS migration, not for this workstream.
