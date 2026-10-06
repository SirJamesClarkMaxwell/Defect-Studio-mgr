# Task 41 S7: GL path renderer, dev add menu, structural GL tests

## Goal
A ScenePath appears in the viewport. `renderScenePaths` turns the store into pixels by taking the
tessellated + stroked geometry out of `PathCaches` and recomputing it only when the evaluation key
misses; `PathDepthMode` decides which of the two passes a path lands in; a dev-only Add submenu puts
Line/Cubic/Arc paths on screen so the first manual round can happen. This is the first stage with
anything visible and the first that touches a shader.

## Files to create
- `src/Renderer/OpenGl/OpenGlPathRenderer.cpp` - defines `OpenGlRendererBackend::renderScenePaths`.
  No header: `OpenGlScenePlaneRenderer.cpp` sets the precedent (a backend method in its own `.cpp`,
  declared in `OpenGlRendererBackend.hpp`), and the plan's separate `OpenGlPathRenderer.hpp` would
  hold nothing the backend header does not already hold.
- `src/Renderer/OpenGl/Shaders/path_tube.vert`
- `src/Renderer/OpenGl/Shaders/path_ribbon.vert`
- `src/Renderer/OpenGl/Shaders/path_stroke.frag` - shared by both programs; the fragment stage is
  identical for a tube and a ribbon (lit per-vertex colour with alpha), so it is one file, not two.
- `src/Presentation/Panels/ScenePathDevMenu.cpp` - implements the already-written header.
- `tests/Renderer/Gl/GlPathRenderTests.cpp`

## Files you may change
- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` - four additions, nothing else:
  1. In `Initialize`, load the two programs next to the existing `LoadGraphicsProgram` calls:
     `"path_tube"` = `path_tube.vert` + `path_stroke.frag`, `"path_ribbon"` = `path_ribbon.vert` +
     `path_stroke.frag`. Same `Result<void>` early-return shape as `arrowQuadLoaded`.
  2. In `RenderWindow`, two calls: the depth-tested pass right after `renderSceneArrows(..., false, ...)`
     and before `renderAtoms`, the always-on-top pass right after the late
     `renderSceneArrows(..., true, ...)` at the end. Both guarded by
     `pathInput != nullptr && pathInput->paths != nullptr`.
  3. In the viewport-resource release loop (next to `sceneArrowMeshCache` / `sceneOrbitalMeshCache`
     cleanup, ~line 1171), `DeleteMeshHandles` every `scenePathMeshCache` entry and clear it.
  4. Nothing else. Do not touch any other pass.
- `src/Renderer/RendererLayer.cpp` - `RenderToFbo` only: build
  `const PathRenderInput pathInput{windowState.paths.get()};` and pass `&pathInput` as the new last
  argument. `Unique::get()` on a const `RendererWindowState` yields a non-const `PathSystem *`
  (unique_ptr constness is shallow) - no `const_cast`.
- `src/Presentation/Panels/RendererPanel.cpp` - one line in the viewport Add submenu:
  `DrawScenePathDevAddMenu(windowState, m_ContextMenuWorldPosition);` after `DrawOrbitalAddMenu`,
  plus the include. Nothing else in that file changes.

## Files that must NOT be touched
- `src/Renderer/OpenGl/OpenGlRendererBackend.hpp` - the contract: `PathRenderInput`,
  `OpenGlScenePathMeshCache`, `scenePathMeshCache`, the `renderScenePaths` declaration and
  `RenderWindow`'s new trailing parameter are already written. If a signature is wrong, STOP and say
  which and why; do not edit it.
- `src/Presentation/Panels/ScenePathDevMenu.hpp` - also the contract, also already written.
- Everything under `src/Renderer/Path/` - S1 to S6, done and green. In particular do NOT add a width
  unit, a dirty flag or a GL handle to any path type.
- Every existing shader in `src/Renderer/OpenGl/Shaders/`.
- `src/Renderer/Scene/`, `src/Renderer/Commands/`, `premake5.lua`, `src/Domain/`, `src/IO/`,
  `src/App/`, every other file in `src/Presentation/`.

## What the pass does, per path
Called twice per frame with opposite `renderAlwaysOnTop`, exactly like `renderSceneArrows`.

1. Walk `input.paths->Store()` with `Visit` or `At`. Skip a path when `!visible`, `!renderable`, its
   `style.depthMode` does not match this call's `renderAlwaysOnTop`, or it has fewer than 2 nodes.
2. **LOD bucket.** Probe screen density at the path's centroid (mean of authored node positions):
   project `centroid` and `centroid + cameraRight` (the camera's right vector) through the
   view-projection, take the pixel distance between the two NDC results using `viewportPixelSize`;
   that is `pixelsPerWorldUnit`. A non-positive `clip.w` on either probe means the path is behind the
   camera - use `kMinLodBucket` rather than a garbage density. Feed it to `QuantiseLod` with
   `previousBucket` = the `lodBucket` of this id's `scenePathMeshCache` entry, or `kNoLodBucket` when
   there is none. That is the whole point of storing the key in the GL cache.
3. **Key.** `PathEvaluationKey{store.RevisionsFor(id), 0, lodBucket}`. `bindingSourceRevision` is 0 in
   S7 on purpose: bindings are resolved against an empty `BindingContext{}` (every node falls back to
   its authored position, which the resolver already handles), and wiring atoms/objects in is S14's
   job. Leave a comment saying so at the one place it is constructed.
4. **Geometry.** `caches.Find(id, key)`; on a miss compute
   `ResolveNodePositions(path, BindingContext{})` ->
   `Tessellate(path, resolved, {ToleranceForLod(lodBucket, 0.5), ...defaults})` ->
   `BuildStroke(evaluated, path.style)` and `caches.Store(id, key, ...)`. Diagnostics are not drawn
   and not logged per frame - drop them.
5. **Upload.** If the id's `scenePathMeshCache` entry is missing or its `key` differs, re-upload VBO +
   EBO and re-declare the vertex attributes (the layout differs between profiles, and re-declaring on
   every upload is why the cache entry carries no layout flag). Uploads happen on a key change only,
   never per frame. Vertex layouts, defined locally in the `.cpp`:
   - tube: `vec3 position; vec3 normal; vec4 color;`
   - ribbon: `vec3 position; vec3 tangent; vec3 normal; vec4 color; float side;`
   `arcT` and `dashCoord` are deliberately not uploaded - the gradient is already baked into `color`
   by `BuildStroke`, and the dash is already cut into runs on the CPU by `BuildDashIntervals`. Do NOT
   add a dash discard to the fragment shader; it would dash an already-dashed mesh.
6. **Drop dead ids** once per frame (in the `renderAlwaysOnTop == false` call only, so the two passes
   do not fight): delete and erase every `scenePathMeshCache` entry whose id no longer resolves in
   the store.
7. **Draw.** `path_tube` for `StrokeProfile::Round`, `path_ribbon` for `Flat` and `CameraFacing`.

## Shaders
`path_tube.vert`: `aPosition/aNormal/aColor` at locations 0/1/2. Uniforms `u_ViewProjection`,
`u_SceneOffset`. Out: world position, normal, colour.

`path_ribbon.vert`: `aPosition/aTangent/aNormal/aColor/aSide` at locations 0..4. Uniforms
`u_ViewProjection`, `u_SceneOffset`, `u_HalfWidth`, `u_CameraFacing` (int), `u_CameraPosition`.
Expansion, in world space:
- `u_CameraFacing == 1`: `offsetDir = normalize(cross(aTangent, normalize(u_CameraPosition - world)))`
- otherwise: `offsetDir = normalize(cross(aTangent, aNormal))` - `aNormal` is the ribbon plane normal
  the mesher transported, so this is the in-plane perpendicular.
- `world += aSide * u_HalfWidth * offsetDir`. Degenerate cross product (parallel inputs) must not
  produce a NaN position: fall back to no offset.
The out normal is the plane normal for Flat and the direction to the camera for CameraFacing, so the
shared fragment stage lights both sanely.

`path_stroke.frag`: the three-light model copied from `bonds.frag` (key/fill/back directions and
intensities, ambient, two-sided flag, camera position, specular intensity, shininess, saturation,
`u_SpecularScale`), applied to the interpolated per-vertex colour and its alpha. Upload
`u_SpecularScale = 0.25` for paths, the same value `renderSceneArrows` uses, for the same reason: an
annotation is a flat colour, not an atom material.

**Stroke width is world-space in S7.** `u_HalfWidth` is `style.width * 0.5f`. The plan's ScreenPixels
unit is deferred: it needs a new field on `PathStrokeStyle` (S5's contract, and persisted from S9),
and CPU-built decorations would keep world width while the shaft went screen-space, which reads as a
bug. `u_HalfWidth` is the uniform a later stage swaps. Do not add the unit here.

## GL state
- `renderAlwaysOnTop == false`: leave the depth test as `configureOpenGlState` set it. If any drawn
  path has effective alpha < 1, disable depth *writes* for the pass and restore after - the same
  thing `renderSceneArrows` does, and for the same reason.
- `renderAlwaysOnTop == true`: `glDisable(GL_DEPTH_TEST)` for the pass, restore after.
- Ribbons are two-sided: `glDisable(GL_CULL_FACE)` around the ribbon draws, restored after. Tubes
  keep whatever culling the frame already has.
- Restore every state you change before returning, on every exit path.
- Wrap the draws in the `TracyGpuZone("Renderer.ScenePaths")` block the other passes use, guarded by
  `#if defined(TRACY_ENABLE)`.

## Dev add menu
`MakeDevScenePath` builds a path centred on `worldPosition`, about 2 Angstrom across, with ids from
`AllocateElementId`, `visible`/`renderable` true, and a style that is obviously visible on the default
background (default Round profile, width ~0.05, an `Arrow` end decoration). Presets:
- `Line`: two nodes, one `LineSegmentData`.
- `Cubic`: two nodes, one `CubicBezierSegmentData` whose handles bow the curve well clear of the
  chord (so a wrong tessellation is visible, not subtle).
- `Arc`: two nodes, one `CircularArcSegmentData` with a finite non-zero `signedSweepRadians` (a right
  angle reads clearly) and a plane normal that is not parallel to the chord.
Every preset must produce a path `ValidatePath` accepts - assert that in a test rather than trusting it.

`DrawScenePathDevAddMenu` opens an `ImGui::BeginMenu("Path (dev)")` with the three items, each
calling `SceneSystem::AppendScenePath(windowState, MakeDevScenePath(preset, worldPosition))` and then
`SceneSystem::SyncLabelEntities(windowState)` (or whatever that function is actually called in
`SceneSystem.hpp` - check, do not guess) so the registry mirror picks the new path up immediately.

## Acceptance criteria
Criteria 1-7 are plain unit tests and need no GL context. 8-16 are `TEST_F(GlTest, ...)` cases in
`tests/Renderer/Gl/GlPathRenderTests.cpp`, following `GlSmokeTests.cpp` exactly, including its
`ShaderDirectoryNextToTestExecutable()` and `PrimitiveMeshes()` helpers (duplicate them locally or
lift them into `GlTestContext.hpp` - your call, but do not leave two diverging copies).

1. `MakeDevScenePath` returns a path with no diagnostics from `ValidatePath` for each of the three
   presets, exactly 2 nodes and 1 segment, and the segment variant matching the preset.
2. Each preset's node positions are finite and its centroid is within 1e-4 of `worldPosition`.
3. Each preset tessellates to at least 2 samples with a finite total length > 0, and the Cubic and
   the Arc produce strictly more samples than the Line at the same tolerance (they are curved).
4. `BuildStroke` on each preset yields non-empty geometry with no non-finite vertex field.
5. `MakeDevScenePath` allocates element ids from the path's own counter: every node and segment id is
   valid and distinct, and `path.nextElementId` is greater than all of them.
6. Two calls with the same preset produce equal geometry (the presets are deterministic - no time, no
   randomness, no global counter).
7. `MakeDevScenePath` leaves `id` unset: `AppendScenePath` is what allocates the `SceneObjectId`, and
   a preset that pre-assigned one would be silently renumbered.
8. `Initialize` on a real context loads both path programs: `RenderWindow` on a window whose
   `PathRenderInput` is null still returns a non-zero texture and the four-corner background test
   from `GlSmokeTests` still holds (the pass is genuinely opt-in).
9. A window holding one Round Line path, drawn head-on, produces pixels that are not the background
   along the path's screen-space centre, and still background outside its width. Assert on a pixel
   count in a band rather than one lucky pixel.
10. Width scales with zoom: the same world-width Line rendered at camera distance `d` and `d / 2`
    covers roughly twice as many non-background pixels across its perpendicular, within a tolerance
    of +/- 1 pixel per edge. This is the world-space-width contract - it is what the plan's pixel-width
    test becomes now that ScreenPixels is deferred.
11. `StrokeProfile::Flat` and `StrokeProfile::Round` of the same path differ: the rendered images are
    not byte-identical, and both cover a non-trivial number of non-background pixels.
12. `CameraFacing` stays facing the camera: render, orbit the camera 90 degrees about the path's own
    axis, render again; the covered pixel count changes by less than 20%. The same test with `Flat`
    must show a substantially larger change (a ribbon seen edge-on nearly vanishes) - assert both, or
    the first half passes for the wrong reason.
13. `PathDepthMode::DepthTest` is occluded: with an atom sphere between the camera and the path, the
    path's centre pixel is the atom's colour, not the path's. With `AlwaysOnTop` and nothing else
    changed, it is the path's.
14. A gradient's end colours land at the ends: sample near each endpoint of a Line with a two-stop
    gradient and assert each is nearer its own stop colour than the other. Use a generous tolerance -
    this asserts the gradient is not reversed or collapsed, not the exact lighting.
15. Cache behaviour, asserted through `PathCaches::Size()` and the store's revisions, not through
    pixels: rendering the same unchanged window twice adds no cache entry the second time; mutating
    a path's style through `MutateStyle` and rendering again produces a hit under the new key;
    erasing a path through `PathSystem::ErasePath` and rendering again leaves no `scenePathMeshCache`
    entry for the dead id.
16. `Shutdown` after paths were drawn leaves no GL object leaked from `scenePathMeshCache` (the
    entries are gone), and a second `Initialize`/`RenderWindow` on the same backend still works.
17. Full Release test suite green except the two permanent skips
    (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
    `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`).

## Constraints
- Layer: `src/Renderer/OpenGl/`, the one line in `src/Presentation/Panels/RendererPanel.cpp`, the new
  `ScenePathDevMenu.cpp`, and `tests/`. Nothing else.
- `Renderer` is the documented exception-free zone - no `throw`, no exceptions on any path; failures
  are return values or an early return. No `std::thread`.
- No new dependency and no new GL extension. Core profile 4.3, same `#version 430 core` every other
  shader uses.
- A missing shader program (`Program(...) == 0`) makes the pass a no-op, not a crash and not a log
  line per frame.
- `.cpp` files stay under ~500 lines. If `OpenGlPathRenderer.cpp` approaches it, split the upload
  helpers into an anonymous namespace at the top rather than a second file.
- Style: tabs, `#include "Core/dspch.hpp"` first, anonymous namespace for helpers, `[[nodiscard]]`.
- Tests: GoogleTest, namespace `DefectStudio::Tests`.

## Manual round (user, after the build is green)
Not your job, but it is what this stage is for, so do not break it: right-click in the viewport ->
Add -> Path (dev) -> Line / Cubic / Arc, orbit and zoom, then export a PNG and compare it to the
viewport. That export comparison also settles S4's deferred gate (the `FrameBufferReadback`
extraction must have left PNG export byte-identical).

## Manual round result (2026-09-26)
Passed. PNG export matches the viewport, which also settles S4's deferred byte-identical
export gate. **Merge point 1 cleared.**
