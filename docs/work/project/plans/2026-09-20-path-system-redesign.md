# PathSystem redesign — scope, architecture, delivery and verification

Status: **superseded by `2026-09-20-path-system-redesign-v2.md`** (kept for history)  
Date: 2026-09-20  
Source: user + Codex design grill, 2026-09-20  
Target: replacement of the editable `SceneArrow`/line/path subsystem

## 1. Goal

Replace the current editable `SceneArrow` implementation with a new `PathSystem` whose numeric
model, evaluated geometry, rendered image, picking and editing behavior agree with one another.
The current runtime is not the architectural base: quadratic rendering, control-point editing and
`Arrow2D` behavior do not meet that contract.

Reuse neutral infrastructure where it remains valid:

- `SceneObjectId` and `SceneRegistry` integration;
- global `Core/Undo` infrastructure;
- project dirty tracking and the `scene_objects.yaml` save flow;
- shader/mesh/FBO infrastructure in the OpenGL backend;
- established panel, Outliner and input conventions.

Do not preserve the current `SceneArrow` model, renderer, interaction code or editor through runtime
adapters. A small IO-only importer is the sole legacy boundary.

## 2. Locked scope

### 2.1 V1 capabilities

V1 is a complete vertical slice:

- ordered, multi-segment, open paths;
- exact segment types: Line, Cubic Bézier and Circular Arc;
- shared junction nodes with stable element identity;
- Bézier handles: Free, Aligned, Vector and Auto;
- mandatory C0 continuity and tangent rules across compatible mixed segment types;
- user-facing render presets Tube3D, FlatRibbon and CameraFacingRibbon, expressed as profile and
  orientation combinations;
- stroke profiles: Round and Flat;
- stroke orientation: ParallelTransport, FixedNormal and CameraFacing;
- joins: Miter, Bevel and Round;
- caps: Butt, Square and Round;
- stroke sizing in WorldUnits or ScreenPixels;
- dash-pattern sizing independently in WorldUnits or ScreenPixels;
- multi-stop color/alpha gradient evaluated over cumulative arc length;
- independent start/end decorations;
- depth modes: DepthTest, AlwaysOnTop and XRay;
- Object Mode and multi-object Edit Mode;
- numeric editing, viewport editing and visible pick handles;
- path bindings to atoms, bond midpoints and object origins;
- command-level undo/redo;
- YAML v2 persistence and one-way v1 import;
- viewport rendering, PNG export and automated graphical regression tests.

### 2.2 Deferred

- cyclic paths and multiple splines in one object;
- modifier stack;
- general constraint stack;
- per-node width and tilt;
- attached labels;
- shared-style assets and custom endpoint assets;
- true paths whose control geometry is stored in screen coordinates;
- FadeWhenOccluded;
- spiral and other additional procedural primitives.

### 2.3 Replacement boundary

The final cutover removes the editable `SceneArrow` runtime in full:

- data types and `RendererWindowState::sceneArrows`;
- arrow geometry builder, 2D quad/SDF path and OpenGL draw pass;
- arrow editor, quick-edit and creation UI;
- arrow picking, gizmos and region selection;
- arrow clipboard and arrow-specific operations;
- arrow Outliner rows and selection state;
- arrow runtime persistence mapping;
- tests that assert obsolete behavior.

Generated displacement arrows also move to the new rendering system at the end of the workstream.
Technical arrows that are not scene annotations, such as navigation widgets, remain independent
unless they can reuse a low-level decoration mesh without taking a PathSystem dependency.

Atoms, bonds, planes, orbitals and labels are not redesigned here. Their rendering receives
incremental golden-image coverage as separate, reviewable additions.

## 3. Architectural boundary

`PathSystem` is a focused Renderer/Scene subsystem owned once per renderer window. It does not live
as another large vector and interaction-state cluster inside `RendererWindowState`, and it does not
trigger a general Scene Architecture v2 rewrite.

Recommended ownership:

```text
RendererLayer
  └─ per-window PathSystem
       ├─ PathStore
       ├─ PathCommandService
       ├─ PathEvaluationCache
       ├─ PathRenderCache
       └─ PathInstanceSet registry

Presentation
  └─ per-window PathEditSession
       ├─ object selection
       ├─ (SceneObjectId, PathElementId) edit selection
       ├─ active element / pivot / orientation
       └─ in-progress command transaction
```

Use `Unique`, `Ref`, references and stable IDs. Do not introduce raw owning pointers.

Suggested source boundaries, subject to repository review:

```text
src/Renderer/Path/
  PathTypes.*
  PathStore.*
  PathSystem.*
  PathEvaluator.*
  PathTessellator.*
  PathStrokeMesher.*
  PathPicking.*
  PathCommands.*
  PathRenderCache.*
  PathInstanceSet.*

src/Renderer/OpenGl/
  OpenGlPathRenderer.*

src/Presentation/Panels/
  PathEditorWidget.*
  ViewportPathInteraction.*
  ViewportPathOverlay.*

src/IO/
  existing scene-object YAML infrastructure extended for ScenePath v2
```

File names are not an instruction to create one class per file. Consolidate where a boundary would
otherwise be shallow, while keeping large OpenGL and interaction files from growing further.

## 4. Runtime data model

Illustrative contract:

```cpp
struct PathElementId { std::uint64_t value; };

struct PathNode {
    PathElementId id;
    glm::vec3 position;
    PathBinding binding;
};

struct LineSegmentData {};

struct PathHandle {
    PathElementId id;
    glm::vec3 position;
    BezierHandleType type;
};

struct CubicBezierSegmentData {
    PathHandle startHandle;
    PathHandle endHandle;
};

struct CircularArcSegmentData {
    glm::vec3 center;
    glm::vec3 normal;
    float radius;
    float startAngleRadians;
    float signedSweepRadians;
};

using PathSegmentData = std::variant<
    LineSegmentData,
    CubicBezierSegmentData,
    CircularArcSegmentData>;

struct PathSegment {
    PathElementId id;
    PathSegmentData data;
};

struct ScenePath {
    SceneObjectId id;
    std::string persistKey;
    std::string name;
    std::vector<PathNode> nodes;
    std::vector<PathSegment> segments;
    StrokeStyle stroke;
    EndpointDecoration startDecoration;
    EndpointDecoration endDecoration;
    DepthMode depthMode;
    bool visible;
    bool renderable;
};
```

Required invariants:

1. For an open path, `segments.size() + 1 == nodes.size()`.
2. Node, segment and editable-handle IDs are stable and unique inside one ScenePath.
3. A segment connects `nodes[i]` to `nodes[i + 1]`; the junction position exists once.
4. Every finite segment remains serializable, including a temporarily degenerate segment.
5. Mutations enter through commands. External code receives const data or narrow operations.
6. Arc edits update their dependent endpoint-node positions atomically.
7. An invalid segment never produces NaN/Inf output or invalidates unrelated segments.

### 4.1 Arc representation review hotspot

The grill selected analytic `center`, `normal`, `radius`, `startAngle` and `signedSweep` as the
numeric source of truth, while also selecting shared path nodes. This creates a deliberate invariant
between analytic arc parameters and endpoint-node positions.

Claude must review whether the implementation should:

1. keep the explicit analytic fields and enforce endpoint synchronization exclusively through
   commands; or
2. store a non-redundant endpoint-based arc representation while preserving the exact same numeric
   editor contract.

The accepted behavior is fixed: users can enter an exact center, axis, radius, start angle and
signed sweep such as `120°`, and adjacent segments observe the resulting shared endpoint. The
internal representation may change if it removes contradictory sources of truth.

### 4.2 Bindings

V1 uses a small extensible value type, not a general constraint stack:

```text
PathBinding
  Free
  CopyPosition(AtomRef, offset, buffer)
  BondMidpoint(AtomRef A, AtomRef B, offset)
  ObjectOrigin(SceneObjectId, offset)
```

Bindings resolve before evaluation. Broken references retain the last finite world position and
produce a structured warning. Dragging a bound node either detaches it explicitly or edits its
offset; the UI must not silently choose between those actions.

## 5. Single geometry contract

All consumers use the same geometry pipeline:

```text
ScenePath + resolved bindings
  → PathEvaluator (analytic segments)
  → PathTessellator (view/export tolerance)
  → EvaluatedPath
       ├─ StrokeMesher
       ├─ endpoint-decoration builder
       ├─ CPU picking
       ├─ viewport overlays
       ├─ arc-length gradient and dash evaluation
       └─ export renderer
```

`EvaluatedPath` contains at least:

- world position;
- tangent;
- transported normal and binormal where applicable;
- segment and element identity;
- segment-local parameter;
- cumulative arc length;
- normalized whole-path coordinate.

Numeric length and exact point/tangent queries use analytic evaluators, not the tessellated samples.
The viewport uses adaptive screen-error tessellation. Export tessellation uses export resolution.
Tests may use a fixed world-space error bound for deterministic expectations.

Straight segments retain a fast path. Adaptive tessellation must have explicit depth/segment limits
and return a diagnostic when the requested tolerance cannot be reached within them.

## 6. Stroke and appearance

One `PathStrokeMesher` consumes `EvaluatedPath`.

```text
StrokeProfile
  Round
  Flat

StrokeOrientation
  ParallelTransport
  FixedNormal
  CameraFacing
```

User-facing presets map to these values:

- Tube3D = Round + ParallelTransport;
- FlatRibbon = Flat + FixedNormal or ParallelTransport;
- CameraFacingRibbon = Flat + CameraFacing.

There is no `Arrow2D` geometry kind. Geometry remains world-space; screen-pixel sizing affects the
rendered stroke dimensions, not the path coordinates.

`StrokeSizeSpace` controls width, outline and endpoint-decoration size. `PatternSpace` independently
controls dash/gap lengths. Changing either enum performs an explicit conversion or assigns a defined
default; it never reinterprets the existing number.

The gradient is a sorted list of `(position, color, alpha)` stops over normalized cumulative arc
length. It does not restart at segment boundaries. Endpoint decorations inherit their side's final
gradient sample unless explicitly overridden.

V1 endpoint kinds:

- None;
- Arrow;
- OpenArrow;
- Stealth;
- Triangle;
- Bar;
- Circle;
- Square;
- Diamond.

Each side is independent. A decoration can be filled where meaningful. Stroke geometry terminates
at the decoration's insertion/base point so the shaft does not show through it. Flat and Round
profiles consume the same semantic decoration definition.

## 7. Editing and picking

### 7.1 Modes

Object Mode selects whole paths and supports multi-selection.

Edit Mode supports elements from multiple paths. Every selection target is qualified by
`(SceneObjectId, PathElementId)`. Commands apply to all compatible selected elements and report
skipped incompatible elements.

Controls:

- `Tab`: Object/Edit Mode;
- `1`: nodes and handles;
- `2`: segments;
- `3`: whole paths;
- `G/R/S`: transform selected elements using existing pivot/orientation conventions;
- `E`: extend each compatible selected active endpoint;
- `Delete`: remove selected elements through a topology-validating command;
- `V`: change Bézier handle type.

Arc overlays expose center, start, end and radius/sweep controls. The numeric panel always shows
the exact values of the active element.

### 7.2 Picking arbitration

Picking uses the same evaluated path, sizes and endpoint geometry as rendering.

Priority in Edit Mode:

1. visible handle;
2. node;
3. endpoint decoration;
4. segment.

Hidden geometry has no hitbox. Drawing and hit-testing of handles share one geometry-producing
helper. Object Mode turns any visible hit into whole-path selection.

GPU ID picking is deferred. CPU picking is appropriate for editable annotations; generated large
sets use PathInstanceSet-specific coarse acceleration if profiling requires it.

## 8. Commands and undo

Every committed mutation is an operation on stable IDs:

- AddPath / DeletePath;
- InsertNode / DeleteNode;
- ReplaceSegmentType;
- MoveNode / MoveHandle;
- SetHandleType;
- SetArcParameters;
- SetStrokeStyle / SetGradient / SetEndpointDecoration;
- SetBinding / DetachBinding;
- ReversePath.

Interactive drags open one transaction on activation, update preview state during movement and push
one undo entry on commit. Cancel restores the exact pre-drag state without adding history.

Commands use the existing global `Core/Undo` stack. They do not join the legacy scene-object
snapshot or create a path-only undo stack. Each successful command updates the owning project's
dirty revision and the narrow cache revision(s) it affects.

## 9. Cache contract

Track at least:

- `geometryRevision`: nodes, segments, bindings and analytic parameters;
- `strokeRevision`: profile, width, joins, caps, pattern and decorations;
- `appearanceRevision`: gradient, opacity and depth mode.

Suggested cache layers:

```text
PathEvaluationCache key
  object id + geometry revision + resolved-binding revision + tolerance/view metric

PathMeshCache key
  evaluation key + stroke revision + render profile

GPU upload/material state
  mesh key + appearance revision
```

A pure color/alpha change must not retessellate the path or rebuild positional mesh data. Camera
changes invalidate CameraFacing/ScreenPixels-derived render geometry but not analytic path data.

## 10. Displacement integration

Displacement arrows migrate last, after the editable path pipeline passes its full acceptance test.

`PathInstanceSet` provides:

- one shared source path or preset;
- one shared stroke/decorations definition;
- per-instance transform, magnitude and optional color-ramp coordinate;
- batched CPU/GPU upload;
- optional promotion of one instance to an editable ScenePath later.

It uses the same evaluator, stroke/decorations vocabulary and shader contract as ScenePath without
creating thousands of Outliner objects, undo commands or YAML entries.

After parity is proven, remove `renderDisplacementArrows` and its private arrow geometry. The
displacement analysis/result model remains unchanged.

## 11. Persistence and migration

`scene_objects.yaml` becomes format version 2 with `kind: ScenePath`. Saving always writes v2.

The parser must:

- load v1 and run a one-way SceneArrow importer;
- load v2 ScenePath entries;
- reject a version newer than supported before interpreting or saving its entries;
- preserve other valid scene objects;
- report per-object migration warnings through structured diagnostics.

Migration rules:

- old quadratic control point → mathematically equivalent cubic controls;
- N explicit points → N−1 Line segments;
- Arrow2D Billboard → Flat + CameraFacing + ScreenPixels;
- Arrow2D FixedPlane → Flat + FixedNormal + ScreenPixels;
- Arrow3D and Line → Round + WorldUnits;
- old start/end tips → corresponding endpoint decorations;
- old two-stop gradient → two gradient stops;
- old atom anchors → node CopyPosition bindings;
- invalid but finite geometry → retained with diagnostic;
- unrepresentable values → explicit migration warning with the chosen fallback.

No legacy `SceneArrow` object reaches runtime after import.

## 12. Graphical regression tests

### 12.1 Harness

Add a test-only RAII hidden OpenGL context:

1. initialize GLFW;
2. request the renderer-compatible core profile and `GLFW_VISIBLE = GLFW_FALSE`;
3. create and bind the window/context;
4. load GLAD;
5. initialize the production renderer with production shaders/assets;
6. render to the existing RGBA8/depth-stencil FBO;
7. read via the existing PNG capture path;
8. destroy the renderer before the context and terminate GLFW.

The test must report `GL_VENDOR`, `GL_RENDERER` and `GL_VERSION`. Missing required GL capability is
an explicit skip outside the canonical graphics lane, not a crash. The canonical graphics
configuration treats the test as a hard gate.

The initial fixture disables time-dependent behavior, text/font rendering, system DPI influence,
multisampling, dithering and unintended sRGB conversion where the production contract permits.
Use fixed camera, viewport, background, lighting and object data.

### 12.2 Pixel comparison

- dimensions and RGBA channel count must match exactly;
- stable interior/background pixels use a tight channel tolerance;
- a one-pixel edge band allows a small channel tolerance for rasterization/antialiasing;
- a small global mismatch budget is enforced;
- bounding box, coverage and selected anchor pixels are checked separately;
- failures write actual PNG, absolute-difference heatmap and mask to a unique temporary directory;
- ordinary test runs never overwrite repository fixtures.

Golden updates require an explicit environment flag scoped to the named test and run on the
canonical graphics machine. Baseline, actual and diff are reviewed before committing.

### 12.3 Incremental golden suite

Add focused images as the relevant render area lands, not as one preliminary mega-task:

1. Line/Cubic/Arc evaluation and joins;
2. Round/Flat/CameraFacing profiles;
3. gradient, dash and endpoint decorations;
4. depth modes;
5. ScenePath editing overlays where deterministic;
6. PathInstanceSet displacement rendering;
7. planes;
8. atoms and bonds;
9. orbitals;
10. labels when font determinism is controlled;
11. final integrated C3 scene with all relevant passes.

## 13. Non-graphical tests

### 13.1 Analytic evaluator

- exact Line positions, tangents and lengths;
- Cubic endpoint/handle behavior and derivatives;
- Circular Arc center/radius/axis/signed-sweep queries, including exact 120°;
- mixed-segment cumulative arc length;
- C0 and requested tangent continuity;
- deterministic Auto/Aligned/Vector/Free handle behavior;
- degenerate and non-finite input diagnostics;
- bounded adaptive tessellation error;
- parallel-transport frame stability and no unintended flips.

### 13.2 Stroke, style and picking

- join/cap topology and finite normals;
- shaft trimming against every endpoint decoration;
- dash phase continuity across segment boundaries;
- multi-stop color/alpha interpolation over whole-path arc length;
- explicit WorldUnits/ScreenPixels conversion;
- rendered-shape picking parity for strokes, decorations, nodes and handles;
- arbitration priority and absence of invisible hitboxes;
- cache invalidation by revision class.

### 13.3 Commands and persistence

- every command apply/undo/redo pair;
- one undo item per drag transaction;
- stable element selection after insert/delete;
- multi-path edit operations and incompatible-element diagnostics;
- YAML v2 round-trip for every segment/style/binding;
- literal v1 SceneArrow fixtures covering Line, Arrow2D, Arrow3D, quadratic, tips, gradient and
  atom anchors;
- future-version rejection;
- save failure leaves the project dirty.

## 14. Manual in-application acceptance

Use one saved scientific scene as the repeated acceptance fixture:

1. Create an exact Circular Arc with center, axis, radius and signed sweep `120°`.
2. Add an endpoint decoration and multi-stop gradient to form a C3 rotation annotation.
3. Create a Cubic Bézier and edit every handle mode in the viewport and numeric panel.
4. Create a dashed Line symmetry axis.
5. Exercise Round, Flat and CameraFacing profiles at several zoom levels.
6. Exercise WorldUnits/ScreenPixels size and pattern spaces without numeric reinterpretation.
7. Exercise DepthTest, AlwaysOnTop and XRay against atoms, bonds, a plane and an orbital.
8. Select nodes/handles/segments across multiple paths; run G/R/S, E, Delete and V.
9. Bind nodes to atoms, a bond midpoint and an object origin; move the sources and verify updates.
10. Undo/redo every class of operation and cancel an in-progress drag.
11. Save, close and reopen; verify values, bindings, appearance and selection behavior.
12. Load a v1 fixture and verify the migration report and resulting ScenePaths.
13. Export PNG and compare it with the viewport and the expected golden.
14. Enable displacement comparison and verify PathInstanceSet parity before deleting the old pass.

No phase that changes user-visible behavior is complete solely because unit tests pass; its relevant
manual subset must also pass in the current application build.

## 15. Delivery stages and gates

### Stage 0 — contracts and baseline

- Add failing evaluator/model tests and define IDs/invariants.
- Add the hidden-GL graphical test fixture with a minimal renderer smoke image.
- Capture a current application fixture only for diagnostic comparison, not as the new golden.

Exit: the new contracts are red for the intended missing behavior; the graphical harness proves it
can produce and compare a deterministic image on the canonical machine.

### Stage 1 — PathData, store and evaluator

- Implement stable IDs, segment/node model, bindings and pure analytic evaluation.
- Implement continuity and handle rules.
- Return structured per-segment diagnostics.

Exit: analytic tests are green; no production renderer integration is required yet.

### Stage 2 — tessellation, frames, stroke mesher and CPU picking

- Implement bounded adaptive tessellation and accumulated arc length.
- Implement frames, profiles, joins, caps, patterns, decorations and gradients.
- Implement picking from the same evaluated/tessellated geometry.

Exit: mesh/picking tests are green and contain no NaN/Inf for degenerate fixtures.

### Stage 3 — PathSystem store, revisions and commands

- Add per-window ownership, cache layers and command mutations.
- Integrate global undo and dirty tracking.
- Cover multi-path editing transactions.

Exit: all command and invalidation tests are green.

### Stage 4 — OpenGL renderer and focused goldens

- Add the production Path renderer.
- Land focused golden images for segment/profile/style/depth behavior as each feature lands.
- Exercise the new renderer through tests and an internal development fixture. Keep the old
  SceneArrow as the still-unmodified application runtime until the atomic editable-system cutover;
  do not route new features through it or create compatibility adapters.

Exit: new renderer goldens and the corresponding manual viewport checks pass.

### Stage 5 — YAML v2 and legacy import

- Implement v2 IO, future-version guard and v1 importer.
- Integrate project save/load and warnings.

Exit: migration and round-trip tests produce only ScenePath data; the application load path is not
switched until Stage 6.

### Stage 6 — Object/Edit Mode and UI

- Add creation presets, Properties editor, numeric controls, overlays, Outliner and input commands.
- Complete multi-object Edit Mode and selection arbitration.
- Atomically switch editable path/line/arrow creation, loading, rendering and interaction to
  ScenePath, then remove the editable SceneArrow runtime. The separate displacement pass remains
  until Stage 8.

Exit: the Line/Cubic/Arc portions of the manual acceptance fixture pass end to end, and the
application has no user-reachable editable SceneArrow path.

### Stage 7 — bindings, export and scientific acceptance scene

- Complete binding UI/refresh and export integration.
- Build and save the exact C3 acceptance scene.
- Add the integrated path + existing-render-passes golden appropriate to this stage.

Exit: steps 1–13 of the manual acceptance protocol pass after save/reopen.

### Stage 8 — PathInstanceSet and displacement cutover

- Implement generated instances on the shared pipeline.
- Compare visual/performance parity with current displacement rendering.
- Remove the old displacement arrow pass only after parity is demonstrated.

Exit: displacement golden, performance bound and manual acceptance step 14 pass.

### Stage 9 — atomic legacy removal

- Remove every remaining SceneArrow type, shader, UI, command, selection field and obsolete test.
- Route all editable path/line/arrow creation to ScenePath.
- Regenerate projects if files changed and update Graphify.

Exit: repository search finds no runtime SceneArrow dependency outside the v1 importer/fixture names;
Debug and Release application/tests build; the full automated and manual matrices pass.

## 16. Claude review checklist

Claude should challenge this draft before implementation, focusing on:

1. whether the analytic-arc/shared-node invariant has one enforceable source of truth;
2. whether per-window PathSystem ownership integrates cleanly with current RendererLayer lifetime;
3. whether multi-path Edit Mode commands are atomic and unambiguous;
4. whether Auto-handle rules are deterministic across mixed segment types;
5. whether screen-pixel stroke tessellation/cache keys include every camera/export dependency;
6. whether endpoint decorations can share semantics across Flat and Round profiles without hidden
   per-renderer behavior;
7. whether the proposed revision split prevents unnecessary mesh rebuilds without stale draws;
8. whether v1→v2 migration preserves bindings and avoids silent data loss;
9. whether the hidden GL fixture has a reliable GL capability contract on Windows and Linux;
10. whether the golden tolerances catch real regressions rather than blessing driver noise;
11. whether PathInstanceSet actually removes, rather than duplicates, displacement rendering;
12. whether any existing arrow consumer was missed by the final deletion audit;
13. whether path storage/evaluation should use float or double precision and where conversion to GPU
    float data belongs;
14. the exact render-pass contract for XRay across opaque and translucent scene geometry.

The review may change internal representation and file boundaries. It must preserve the locked user
behavior, replacement boundary, staged displacement cutover and verification requirements above,
or call out the requested design decision explicitly before changing them.
