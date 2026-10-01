# PathSystem — implementation plan (branch `task/41-path-system`)

Status: draft, written on the branch it drives
Date: 2026-09-20
Design contract: `2026-09-20-path-system-redesign-v2.md` (wins over this file on any design question).
This file answers "how, in what order, in which files, proven by what".

## 0. Branch model

One branch, `task/41-path-system`, cut from `dev` at `426e975` (= `task/40`, merged by fast-forward).
The work is linear (every stage builds on the previous), so v2's sixteen "branches" become sixteen
**stages = sixteen commits or short commit runs** on this one branch.

Rules that keep a long branch safe:

1. **Every stage ends green**: Release build of `DefectStudio.exe` and `DefectStudioTests.exe`, all tests
   pass (2 permanent skips expected), via the `full-build-verify` skill (Release-only during active dev,
   per project memory). Commit message: `task/41 S<n>: <what>`.
2. **Nothing user-visible changes until S15.** The old SceneArrow stays the running system; ScenePath is
   reachable only through a non-default dev switch (S7 onward). Therefore `dev` may take the branch at any
   green stage. Suggested merge points: after S7 (renderer proven), after S9 (persistence proven), after S14.
3. **Merge `dev` into the branch** before each of those points (54 src files reference SceneArrow, other
   tasks touch them; conflicts are cheapest when resolved early and often).
4. `.cpp` ≤ ~500 lines; regenerate projects after adding any file (`scripts/Windows/GenerateProjects.bat`,
   premake globs at generation time); `architecture-boundary-review` at each merge point; Graphify
   update at S16.
5. Contract-first per `dispatch-codex-task`: I write headers + failing tests, build to confirm they fail for
   the right reason, then Codex writes the `.cpp`. Per project memory, Codex does recon + implement +
   build/test + self-review, is told "don't build, no approval step" if it cannot build in its sandbox, and
   I verify. Stages are chained and committed one by one; the user is called once per combined manual round
   (S7, S9, S11, S13, S14), not per stage.
6. A stage that discovers a design flaw stops and edits v2 first, then resumes.

## 1. Stage overview

| S | Pure C++ / needs GL / needs UI | Key deliverable | Depends on |
|---|-------------------------------|-----------------|-----------|
| 1 | pure | Model, IDs, evaluator (Line/Cubic/Arc), binding resolver | – |
| 2 | pure | Topology operations, handle rules, Make Tangent | 1 |
| 3 | pure | Adaptive tessellation, LOD buckets, transported frames | 1 |
| 4 | GL | Hidden-context test harness, shared readback helper | – (parallel to 1–3) |
| 5 | pure | Stroke mesh data, joins/caps, dashes, gradient, 8 decorations | 3 |
| 6 | app-state | `Unique<PathSystem>`, kind + registry mirror, snapshot slice, caches | 1, 3, 5 |
| 7 | GL | Path renderer, shaders, DepthTest/AlwaysOnTop, dev switch | 4, 5, 6 |
| 8 | commands | Path commands, keymaps, undo integration, dirty events | 6 |
| 9 | IO | YAML v2, v1 migration, `.v1.bak` | 6 |
| 10 | picking | CPU picking from evaluated geometry | 3, 5, 6 |
| 11 | UI | Object Mode: create, Outliner, Properties, clipboard, delete | 8, 9, 10 |
| 12 | UI | Edit Mode: element selection, overlays, drag, G/R/S | 11 |
| 13 | UI | Extend/delete/insert/handle-type/reverse, numeric arc editor | 2, 12 |
| 14 | UI | Bindings UI/refresh, export, saved C3 acceptance scene | 13 |
| 15 | routing | Cutover (default-route flip) | 14 |
| 16 | deletion | Legacy removal | 15 |

Conventions used below: new code lives in `src/Renderer/Path/` (namespace `DefectStudio`, tabs, `Result<T>` +
`StructuredError`, `Unique`/`Ref`, no raw owning pointers); tests in `tests/Renderer/Path/`. `float` storage,
`double` evaluation (v2 C9). Ids: `SceneObjectId` is reused from `Renderer/Scene/SceneObject.hpp`.

---

## S1 — model, evaluator, binding resolver (pure)

**Files (new):** `Path/PathTypes.hpp`, `Path/PathEvaluator.{hpp,cpp}`, `Path/PathBindingResolver.{hpp,cpp}`,
`tests/Renderer/Path/PathModelTests.cpp`, `PathEvaluatorTests.cpp`, `PathBindingResolverTests.cpp`.
**Not touched:** every existing file except premake regeneration.
Scope note: v2 puts the "reuse lift" (tip table, dash logic) in branch 1. It moves to **S5**, the first
consumer — lifting code with no consumer is churn.

**Contract (headers I write):**

```cpp
struct PathElementId { std::uint64_t value = 0; /* IsValid, ==, <=> like SceneObjectId */ };
enum class BezierHandleType { Free, Aligned, Vector, Auto };
struct PathHandle { PathElementId id; glm::vec3 position{0}; BezierHandleType type = BezierHandleType::Auto; };

struct PathBinding {                       // small closed variant, not a constraint stack
  struct Free {};
  struct CopyPosition { std::size_t atomIndex; glm::vec3 offset{0}; float buffer = 0.f; }; // atom radii
  struct BondMidpoint { std::size_t atomA, atomB; glm::vec3 offset{0}; };
  struct ObjectOrigin { SceneObjectId object; glm::vec3 offset{0}; };
  std::variant<Free, CopyPosition, BondMidpoint, ObjectOrigin> value;
};
struct PathNode { PathElementId id; glm::vec3 position{0}; PathBinding binding; }; // position = authored/frozen fallback
struct LineSegmentData {};
struct CubicBezierSegmentData { PathHandle startHandle, endHandle; };              // absolute positions
struct CircularArcSegmentData { glm::vec3 planeNormal{0,0,1}; float signedSweepRadians = 0.f; };
using PathSegmentData = std::variant<LineSegmentData, CubicBezierSegmentData, CircularArcSegmentData>;
struct PathSegment { PathElementId id; PathSegmentData data; };
struct ScenePath {                         // style/decorations/depth arrive in S5; no dead fields now
  SceneObjectId id; std::string persistKey, name;
  std::vector<PathNode> nodes; std::vector<PathSegment> segments;
  std::uint64_t nextElementId = 1; bool visible = true, renderable = true;
};
[[nodiscard]] PathElementId AllocateElementId(ScenePath&);
```

```cpp
enum class PathDiagnosticCode { NodeCountMismatch, DuplicateElementId, NonFinite, ZeroChord,
  ArcNormalParallelToChord, ArcSweepOutOfRange, ArcNonFiniteDerived, BrokenBinding, ObjectOriginTargetsPath };
struct PathDiagnostic { PathDiagnosticCode code; PathElementId element; std::string message; };
[[nodiscard]] std::vector<PathDiagnostic> ValidatePath(const ScenePath&);   // invariants 1-4, 7 of v2 s4

struct ArcGeometry { glm::dvec3 center, normal, startDirection; double radius, startAngle, signedSweep; };
[[nodiscard]] Result<ArcGeometry> DeriveArc(glm::dvec3 a, glm::dvec3 b, glm::vec3 normal, float signedSweep);
                         // r = |AB|/(2 sin(|θ|/2)), ε ≤ |θ| ≤ 2π−ε, normal re-orthogonalised against AB

struct PathSample { glm::dvec3 position, tangent; };            // tangent unit, direction of travel
struct ResolvedNodes { std::vector<glm::vec3> positions; std::vector<PathDiagnostic> diagnostics; };

[[nodiscard]] Result<PathSample> EvaluateSegment(const ScenePath&, const ResolvedNodes&, std::size_t seg, double t);
[[nodiscard]] Result<double>     SegmentLength(const ScenePath&, const ResolvedNodes&, std::size_t seg);
[[nodiscard]] Result<double>     SegmentParamAtLength(const ScenePath&, const ResolvedNodes&, std::size_t seg, double s);
[[nodiscard]] std::vector<double> CumulativeLengths(const ScenePath&, const ResolvedNodes&); // size = segments+1
```

```cpp
struct BindingContext {                    // narrow view of the scene; keeps resolver pure and testable
  std::function<std::optional<glm::vec3>(std::size_t)> atomPosition;
  std::function<std::optional<float>(std::size_t)>     atomRadius;
  std::function<std::optional<glm::vec3>(SceneObjectId)> objectOrigin;
  std::function<bool(SceneObjectId)> isScenePath;
};
[[nodiscard]] ResolvedNodes ResolveNodePositions(const ScenePath&, const BindingContext&); // const: never mutates authored data
```

**Rules encoded in tests (each a `TEST`):**
- Line: position at t, unit tangent, length = chord.
- Cubic: endpoints exact, t=0/1 tangent = handle direction, length monotone in handle length, symmetric S-curve length known within 1e-9 (fixed Gauss-Legendre with subdivision; documented as bounded-error integration, not sample summation).
- Arc: 120° sweep gives r = |AB|/√3; quarter/half/`−`sweep; endpoint hit exactly at t=1 within 1e-12; length = r|θ|; sweep sign flips travel direction; invalid normal/chord/sweep return the named `PathDiagnosticCode`, never NaN.
- `max_digits10` round-trip of `signedSweepRadians` for 120° (float → text → float bit-identical).
- Mixed path: `CumulativeLengths` sums Line+Cubic+Arc; `SegmentParamAtLength` inverts `SegmentLength`.
- `ValidatePath`: N nodes / N−1 segments, duplicate ids, NaN.
- Resolver: Free returns authored; CopyPosition with offset; endpoint buffer offsets toward the neighbour's **unbuffered** position with the existing non-inversion clamp (mirror `SceneSystem.cpp:328` semantics); interior node with `buffer != 0` → diagnostic; BondMidpoint; ObjectOrigin; `ObjectOriginTargetsPath` diagnostic; missing atom → authored fallback + `BrokenBinding`; resolver leaves the input `ScenePath` byte-identical.

**Gate:** all above green; `SceneArrowGeometryTests` untouched and green; zero warnings-as-errors.

## S2 — topology (pure)

**Files:** `Path/PathTopology.{hpp,cpp}`, `PathHandleRules.{hpp,cpp}`, tests `PathTopologyTests.cpp`.
All operations are `Result<void>` on a `ScenePath&`, implemented on a working copy and committed only on
success (atomicity by construction; multi-path validate-then-apply comes in S8).

```cpp
Result<PathElementId> InsertNode(ScenePath&, std::size_t segment, double t);   // Line/de Casteljau/arc-sweep split
Result<PathElementId> ExtendEnd(ScenePath&, PathEnd end, glm::vec3 newPosition); // inherits terminal type
Result<void> DeleteNode(ScenePath&, PathElementId);      // Line+Line merges; else StructuredError; last node rejected
Result<void> ReversePath(ScenePath&);                    // nodes, segments, handle swap, sweep negate
Result<void> MakeTangent(ScenePath&, PathElementId node);
Result<void> ApplyAutoHandles(ScenePath&);               // v2 s3 rules; never changes a rigid segment's type
```

**Tests:** split then re-evaluate equals original curve (positions within 1e-6 on a 64-point sweep - the
original 1e-9 predates the float-storage decision in v2 C9 and is unreachable once a split node is stored
as `glm::vec3`) for Line,
Cubic, Arc; `Reverse∘Reverse` = identity (nodes, handles, sweeps); delete matrix (Line+Line ok, Line+Arc
rejected with the code, endpoint delete, last-node rejected); extend inherits type; Make-Tangent on
Cubic–Cubic (opposite rays, ⅓ chord), Cubic–Line, Cubic–Arc (aligns to end tangent), Line–Arc stays a corner,
degenerate returns a diagnostic; rejected op leaves the path byte-identical.
**Gate:** green. (Reverse also swaps decorations/gradient/dash phase — added when S5 introduces them, with a
test then.)

## S3 — tessellation and frames (pure)

**Files:** `Path/PathTessellator.{hpp,cpp}`, `PathFrames.{hpp,cpp}`, `PathLod.{hpp,cpp}`, tests.

```cpp
struct TessellationSettings { double worldTolerance; int maxDepth = 12; int maxSamplesPerSegment = 4096; };
struct EvaluatedSample { glm::dvec3 position, tangent, normal, binormal; PathElementId segment;
                         double localT, arcLength, normalizedT; };
struct EvaluatedPath { std::vector<EvaluatedSample> samples; std::vector<PathDiagnostic> diagnostics; double totalLength; };
EvaluatedPath Tessellate(const ScenePath&, const ResolvedNodes&, const TessellationSettings&);
int QuantiseLod(double pixelsPerWorldUnit, int previousBucket);   // power-of-two buckets + hysteresis band
double ToleranceForLod(int bucket, double pixelErrorBudget);
```

- Straight segments: two samples (fast path). Cubic/Arc: recursive subdivision on chord-height error; depth and
  sample caps produce a `diagnostic` instead of silently exceeding tolerance.
- Frames: parallel transport seeded from arc `normal` (Arc) or a stable world-up fallback; frame carried across
  segment boundaries; a `FixedNormal` variant projects the given normal.
- **Tests:** error ≤ tolerance against an analytic dense reference (Cubic, Arc, mixed); sample counts monotone
  in tolerance; samples never NaN/Inf for degenerate fixtures; no frame flip over a full-circle-minus-ε arc and
  a planar S-curve; arc length of `EvaluatedPath` within tolerance of `CumulativeLengths`; LOD hysteresis: a
  sweep of zoom values around a bucket boundary changes bucket at most once per crossing.
**Gate:** green.

## S4 — GL test harness (parallel, independent of paths)

**Files:** `tests/Renderer/Gl/GlTestContext.{hpp,cpp}` (RAII: `glfwInit`, hidden window, GLAD load, teardown in
reverse order), `tests/Renderer/Gl/GlSmokeTests.cpp`; new `src/Renderer/OpenGl/FrameBufferReadback.{hpp,cpp}`
(`ReadRgba8TopDown(width,height,…)`) extracted from the PNG exporter (`OpenGlRendererBackend.cpp:~3443`) and
called by it; `premake5.lua`: test target copies `src/Renderer/OpenGl/Shaders` next to the test exe and links what
GL needs (test target already links GLFW).
- Fixture: fixed viewport, camera, background, lighting; no multisampling/dither/sRGB conversion surprises;
  logs `GL_VENDOR/RENDERER/VERSION`; serialised via a global gtest environment lock.
- **Windows: missing required capability fails.** Linux: capable environments run the structural assertions,
  missing capability → explicit `GTEST_SKIP` with reason.
- Smoke tests: production `OpenGlRendererBackend::Initialize` with the real shader dir and primitive meshes;
  render an empty structure to the FBO; assert exact background RGBA at 4 corners; assert readback flip
  orientation with a known asymmetric fixture.
- **Risk:** this stage modifies production code (exporter now calls the helper). Manual check: export a PNG
  before/after and compare bytes on the same scene.
**Gate:** smoke green on this machine; PNG export byte-identical to pre-refactor.

## S5 — stroke, dashes, gradient, decorations (pure)

**Files:** `Path/PathStyle.hpp` (StrokeStyle, gradient stops, EndpointDecoration, DepthMode; `ScenePath` gains the
fields now — S1 tests keep compiling because members default), `PathStrokeMesher.{hpp,cpp}`, `PathDash.{hpp,cpp}`,
`PathDecoration.{hpp,cpp}`, tests.
**Reuse:** lift the arc-length dash-interval logic from `SceneArrowGeometry.cpp:255` into `PathDash` (legacy
wrapper calls it; `SceneArrowGeometryTests` stay green). The tip table is *replaced* by contours (below);
`GetArrowTipParameters` stays for the legacy path until S16.
- **Outputs (CPU, camera-independent):** (a) Round: indexed tube mesh (position, normal, arcT, dashCoord);
  (b) Flat/CameraFacing: centreline ribbon data (position, tangent, side ∈ {−1,+1}, arcT, dashCoord) for
  shader-side expansion in S7. Joins Bevel/Round (finite normals at bends), caps Butt/Square/Round.
- Dashes: world-space, phase continuous across segments; `PathDash` returns on/off intervals over `[0,L]`.
- Gradient: sorted `(p, colour, alpha)` stops over normalised arc length; sampled per vertex; does not restart
  at segments.
- Decorations = axial contour `{points (s, halfWidth), filled, closesBack, trim}`; eight kinds:
  None, Arrow(=Plain), Stealth(=Barbed), OpenArrow(=Open, two strokes), Bar, Circle, Square, Diamond.
  Round revolves the contour, Flat triangulates it in the ribbon plane; OpenArrow in Round = planar two-stroke
  decoration oriented by the endpoint frame. `TrimmedRange(path, decoration)` shortens the shaft.
- `ReversePath` (S2) extended: swap decorations, mirror gradient stops, adjust dash phase; test added.
- **Tests:** join/cap topology; finite normals at 0°/90°/180° bends; dash coverage sums; phase continuity across a
  Line→Arc boundary; gradient endpoints + mid-stop alpha; every decoration produces closed/non-degenerate
  geometry in both profiles; trim distance equals decoration insertion length; no NaN/Inf for degenerate paths.
**Gate:** green.

## S6 — ownership, kind, registry mirror, snapshot slice, caches

**Files (new):** `Path/PathStore.{hpp,cpp}` (copyable, id-based API: find/insert/erase/visit, per-path
revisions), `PathSystem.{hpp,cpp}` (owns store + non-copyable caches), `PathCaches.{hpp,cpp}`.
**Files (changed, minimal):** `RendererWindowState.hpp` (**one** member `Unique<PathSystem> paths`),
`Scene/SceneObject.hpp` (`SceneObjectKind::ScenePath`), `Scene/SceneSystem.{hpp,cpp}` (mirror sync for paths,
following the arrow pattern at `SceneSystem.cpp:274`), `Commands/SceneObjectsSnapshotCommand.{hpp,cpp}`
(snapshot gains `PathStore` copy; capture/restore next to `sceneArrows`), `RendererLayer.cpp`
(`PopulateExportPreviewState` copies the store).
- Revisions: `geometryRevision`, `styleRevision` per path; resolved-binding source revision folded into the
  evaluation key; eviction on erase / window close / GL shutdown / preview destruction.
- **Tests:** insert/erase/find by id; store copy is deep; snapshot capture → mutate → restore round-trips;
  mirror sync creates one entity per path with stable `SceneObjectId`; isolated invalidation (mutating path A does
  not bump B); export-preview copy independent of the source; window move (vector reallocation) keeps the
  `Unique<PathSystem>` valid.
- **Watch:** `RendererWindowState.hpp` is 664 lines and central — keep the diff to the one member + include.
**Gate:** green; no behaviour change (no path exists at runtime yet).

## S7 — GL renderer + dev switch  *(first manual round)*

**Files:** `src/Renderer/OpenGl/OpenGlPathRenderer.{hpp,cpp}`, shaders `path_ribbon.{vert,frag}` and reuse of
the bond shader for Round tubes (as arrows do today), backend hook near the existing arrow passes
(`OpenGlRendererBackend.cpp:1041–1078` — opaque path pass before atoms, `AlwaysOnTop` in the late
depth-disabled pass next to Arrow2D). Pass paths via a small `PathRenderInput` struct instead of another default
argument on the already huge `RenderWindow(...)` signature.
- Shader-side: CameraFacing basis, ScreenPixels width (uses viewport pixel size and export scale
  `targetHeight / sourceViewportHeight`), gradient/alpha, dash discard.
- **Dev switch:** a developer-only setting (checked in `RendererSettings`; hidden menu item "Add path (dev)").
  Default off; the shipping UI cannot reach ScenePath.
- **Structural GL tests (S4 harness):** a Line path of width 6 px renders 6±1 px wide at two zoom levels; Flat vs
  Round differ as expected; CameraFacing ribbon stays screen-facing after camera rotation; DepthTest path is
  occluded by an atom sphere, AlwaysOnTop is not; gradient end pixels match end colours; Square/Diamond visible.
- PNG goldens: only if GL strings match the recorded baseline (v2 C13).
**Manual round (user):** dev-add Line/Cubic/Arc paths, orbit/zoom, export PNG, compare to viewport.
**Gate:** structural tests green; manual round passes. **Merge point 1.**

## S8 — commands, undo, dirty

**Files:** `Path/PathCommands.{hpp,cpp}`; registration in `Renderer/Commands/RendererCommandRegistration.cpp`;
keymap entries; scene-object-modified event after mutation (`RendererLayer.cpp:52` → `EditorLayer.cpp:774`).
- One command per v2 op list (Add/DeletePath, InsertNode/DeleteNode, MoveNode/MoveHandle, SetHandleType,
  SetArcParameters (atomic solver → endpoint nodes), Set*Style, SetBinding/Detach, ReversePath), each an
  `ICommand` on Core/Undo, grouped with `UndoScope`. Mixed operations keep using the snapshot (now including
  paths). Drag transactions: begin/update/commit = one undo item; cancel = exact pre-drag state, no history.
- Multi-path commands: validate whole target set first, skip + report incompatible elements.
- **Tests (UndoStack-level, no UI):** apply/undo/redo per command; one item per drag; cancel leaves stack
  depth unchanged; mixed atom+path move undoes once and restores both; dirty event fires on success only.
**Gate:** green; no raw shortcut mutation added (grep check).

## S9 — persistence and migration  *(second manual round)*

**Files:** `IO/SceneObjectsIO.{hpp,cpp}` (v2 DTO `ScenePathData`, `kFormatVersion = 2`, future-version guard
before interpretation, `WriteBackupOnce` → `scene_objects.yaml.v1.bak`, failure aborts save with
`StructuredError`), `Renderer/Scene/ScenePathPersistence.{hpp,cpp}` (DTO ⇄ `ScenePath`, v1 arrow → path
migration with diagnostics), `SceneObjectPersistence.cpp` (dispatch). Boundary: IO parses DTOs only; conversion
lives in `Renderer/Scene`.
- Migration per v2 s4 (quadratic→cubic `⅔` elevation for two-point paths only; N points → N−1 Lines;
  Arrow2D/3D/Line → profile/orientation/space; tip renames; gradient; alpha; atom anchors → endpoint
  CopyPosition with buffer; non-zero outline / `curveSegments` → warning). Element ids regenerated on load.
- **Tests:** literal v1 fixtures under `tests/fixtures/scene_objects_v1/` (Line, Arrow2D Billboard, Arrow2D
  FixedPlane, Arrow3D, quadratic, tips, gradient, atom anchors, outline≠0); v2 round trip for every segment,
  style, decoration, binding; future version rejected; other object kinds preserved; backup exists and is not
  overwritten; failing backup blocks save and leaves the project dirty.
**Manual round:** open the user's real project(s) with old arrows in the dev route, read the migration report.
**Gate:** green; merge dev in. **Merge point 2.**

## S10 — picking

**Files:** `Path/PathPicking.{hpp,cpp}`, `Path/PathHandleGeometry.{hpp,cpp}` (single helper shared by drawing and
hit-testing); wiring in `Presentation/Panels/ViewportPicking.cpp` + `Renderer/Scene/SelectionHitTest.cpp`.
- Priority: handle > node > decoration > segment; hidden geometry has no hitbox; hit radii reuse the task/40
  values (20/26 px). Picking consumes the same `EvaluatedPath`/stroke sizes/decoration contours as rendering.
- **Tests:** render/pick parity on fixtures (a pixel inside the drawn stroke hits; a pixel 1 px outside the
  stroke+tolerance does not); decoration hit; arbitration order; invisible handle not pickable; Object Mode
  turns any hit into whole-path selection.

## S11 — Object Mode UI  *(third manual round)*

Touches the existing scene-object surfaces, kept mechanical: `Presentation/Panels/ScenePathEditorWidget.*`
(Properties), `SceneOutlinerRows/Panel`, `ObjectPropertiesPanelSections`, `SceneObjectEditActions`
(delete/copy/duplicate/paste), `ViewportRegionSelect`, `SceneVisibility`, `SceneObjectAppearance`, creation
presets (Tube3D / FlatRibbon / CameraFacingRibbon, Line/Cubic/Arc) behind the dev switch. Object selection uses
the existing unified selection; mixed G/R/S already flows through `ModalTransform` — extend it to move path
nodes as world-coordinate points (V1 has no object transform).
- **Tests:** presentation-level logic tests in the style of `SceneArrowOperationsTests` (clipboard, delete,
  duplicate keep ids/undo/dirty); visibility eye/camera.
**Manual round:** create/select/delete/copy/paste/duplicate/hide paths alongside atoms and planes.

## S12 — Edit Mode

`Presentation/Panels/ViewportPathInteraction.cpp`, `ViewportPathOverlay.cpp`, `PathEditSession.*` (element
selection `(SceneObjectId, PathElementId)`, active element, pivot/orientation, in-progress transaction). `Tab`
toggles; `1/2/3` node-handle / segment / whole path; G/R/S over selected elements via existing pivot/orientation
conventions; overlays drawn with the S10 handle helper. Actions go through `CommandRegistry`.
- **Tests:** session logic (selection stays stable across insert/delete); transactions: one undo item, cancel
  restores state.

## S13 — topology UI + numeric arc editor  *(fourth manual round)*

Path Edit Mode keys now route through CommandRegistry + CommandService + keymap (task 48a).

`E` extend, `Delete`, `V` handle type, insert-on-segment, reverse — wired to S2 ops through S8 commands with the
skip/diagnostic report. Numeric panel shows exact centre/axis/radius/start/sweep of the active arc; edits run
the atomic solver. **Manual round:** the topology matrix and the exact-120° arc.

## S14 — bindings, export, acceptance  *(fifth manual round)*

Status 2026-10-02: live binding resolution in render/export/pick/overlay/region/pivots and the BindingSourceRevision cache key (task 50a); binding UI, Detach-keeping-position and G/R/S-moves-offset (task 50b); legacy inventory in `path-legacy-inventory.md`. **Open:** ObjectOrigin binding (needs a persistence second pass), the saved C3 acceptance scene, and the manual acceptance run.

Binding UI (atom, bond midpoint, object origin; buffer only on endpoints; explicit detach-vs-edit-offset choice),
refresh at the place `RefreshAnchoredSceneArrows` runs (`ViewportInteraction.cpp:30`) as a non-mutating resolve;
export integration; a saved C3 acceptance scene committed under `tests/fixtures/` or `docs/`; run acceptance steps
1–13 of the original plan's §14 as amended (no XRay, no ScreenPixels dash, no Miter/outline/Triangle).
**Also produce the legacy inventory:** `grep -rlE "SceneArrow|sceneArrows|selectedSceneArrows|sceneArrow" src tests`
classified as compile-time replacement / runtime routing / serialization / behavioural parity, saved as
`docs/work/project/plans/path-legacy-inventory.md`. **Merge point 3** once this passes.

## S15 — cutover (routing only)

Allowed only if S14's route already covers: creation, load/save, rendering, export preview, visibility, picking,
region selection, Outliner/Properties, clipboard, delete/duplicate, mixed G/R/S, Edit Mode, bindings, undo, dirty.
Changes limited to: dev switch default → on, all creation entry points → ScenePath, load path → v2/migration,
legacy reachability removed from menus/shortcuts. Anything else discovered goes back to its owning stage.
Displacement arrows untouched.

## S16 — legacy removal

Delete per the inventory: `SceneArrow` types and `RendererWindowState::sceneArrows/selectedSceneArrows/…`,
`SceneArrowGeometry`, `SceneArrowEditorWidget`, `SceneArrowOperations`, `ViewportSceneArrowInteraction`, arrow
shaders and draw passes, legacy tests, legacy wrappers, `SceneObjectKind::SceneArrow`. Regenerate projects, Debug +
Release full matrix, `architecture-boundary-review`, Graphify update. **Gate:** search finds legacy names only in
the v1 DTO/importer/fixtures.

---

## 2. Cross-cutting risks and mitigations

| Risk | Mitigation |
|------|-----------|
| Long branch diverges from `dev` | Merge `dev` in at every merge point; stages 1–5 touch no existing files |
| S6 edits central files (`RendererWindowState.hpp`, snapshot command) | Smallest possible diff, own commit, tests first |
| S4 changes production PNG export | Byte-identical export check before/after |
| S9 rewrites user data format | `.v1.bak` first, migration report reviewed on the user's real projects |
| Scope creep from the old arrow feature set | The v2 §2 cut list is binding; new asks go to a follow-up |
| Uncommitted work in the tree (task 34 `ProjectTreePanel`) | Kept out of every commit (`git add` by path); build verification runs on what is on disk — move it to its own branch before S1's first verify |

## 3. Immediate next actions (S1)

1. Write `Path/PathTypes.hpp`, `PathEvaluator.hpp`, `PathBindingResolver.hpp` exactly as in S1.
2. Write the three test files with the listed cases; regenerate projects; build and confirm the tests **fail to
   link/run for the right reason** (missing `.cpp`), not for typos.
3. Write `docs/work/project/tasks/41-path-system-s1.md` (goal, files, must-not-touch list, acceptance, constraints)
   and dispatch Codex with a prompt file in the scratchpad.
4. Verify: `full-build-verify` (Release), `architecture-boundary-review`, read the diff, commit `task/41 S1: …`.
