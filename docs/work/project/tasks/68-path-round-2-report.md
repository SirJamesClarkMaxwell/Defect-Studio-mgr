# Task 68 implementation report

Implemented the four path changes. Compilation and executable tests are left to the caller, as
required by the task. No commit was made.

## Tail geometry

The ring-based shaft mesher emitted caps only for `Round`: curved Butt/Square solids had an open
undecorated tail, exposing their interior/back faces. Straight plain ribbons used a different,
closed meshing branch, which made the failure depend on the path/style. Butt/Square ends now reuse
the existing ring fan, with duplicated boundary vertices for hard cap normals. Square extensions
also respect decoration attachment, rather than extending a decorated boundary into its tip.

The bevel emitter now constrains terminal-ring vertices to the cap's inward half-space, including
corner patches. This prevents a miter/profile from protruding behind a sharply angled start.
Larger sharp solid faces use the existing concave-polygon triangulator. Source triangles/quads
retain their direct triangulation in the sharp fallback, including folded handoff strips.
The screenshots have not been reproduced in the application: the caller should verify the visual
result after building.

## Insert tool

Ctrl+R starts a persistent modal, including from Object Mode with one selected path. It can start
without a hit, re-picks every hovered frame with 48 pixels of extra tolerance, and retains the last
segment on a miss. Wheel, +/-, keypad +/- and PageUp/PageDown change the count within 1..64.
LMB/Enter commit; Escape/RMB cancel. RMB release is consumed so cancellation does not also open
the context menu. The requested Polish cursor hint and an Edit Mode RMB menu
item are included. The runtime binding retains the existing user-override behavior.

The existing de Casteljau, linear, and angle-based splitting already preserve geometry; those
algorithms were retained. Both the modal and topology validation now accept up to 64 nodes.

## Curved-arrow action

`Result<SceneObjectId> AddCurvedArrowThroughSelectedAtoms(RendererWindowState&)` is declared in
`ScenePathOperations.hpp` and implemented in `ScenePathOperations.cpp`, ready for task 69's Add
menus. This task adds no Add-menu entries.

Axis priority: defect-frame z/origin, then one selected vacancy for two atom ends, then the plane
through the ends and the nearest three other atoms, with the axis through the midpoint. Degenerate
planes use a world basis least parallel to the chord. The short signed sweep is used.

Equal-radius, coplanar ends without clearance use an exact circular arc. Requested atom clearance
or unequal radii/heights use circular Bezier controls with `k = 4/3 tan(theta/4)`: the chord-based
binding buffer cannot shorten a circular arc's stored sweep. Controls compensate the initial
buffer shifts so the intended circular midpoint is retained, including unequal atom radii. Both
ends retain their atom/vacancy bindings and the current buffer. Subsequent motion follows the
nodes through the existing resolver; it does not impose a new circular constraint on moving atoms.

The unchanged free-segment creation and buffer accessor were moved to `SceneFreePathCreation.cpp`
to keep `ScenePathOperations.cpp` below 500 lines.

## Shading and depth

The path fragment shader already implements the bond shader's ambient/diffuse/Blinn-Phong terms
and uses the same global lights. The path upload reduced specular strength to 0.25; it now uses
the bond value, 1.0. Solid normal averaging is limited to matching surface groups: longitudinal
faces smooth together, while caps, bevel boundaries and sharp profile seams remain separate.
The normal accumulator is explicitly initialized.

Normal paths explicitly enable depth testing. Opaque paths write depth, transparent paths do
not, and opaque paths are submitted before transparent paths. The normal pass follows opaque
atoms, so transparent paths blend against atom color/depth rather than being overwritten by the
later atom pass. Always-on-top paths and the Edit Mode overlay retain their dedicated behavior.

## Validation

- Added `PathRibbonTailTests`: sharp-start cubic ribbon, gradient, bevel/no bevel, two tessellation
  tolerances, cap presence/winding/normals, terminal half-space and hard normal boundaries.
- Expanded `PathLoopCutTests`: three-node cubic/arc splits through both topology and live commands,
  transformed geometry with deviation below 1e-4, 64-node limits, generous picking, empty preview
  and segment re-picking without losing the count. Existing undo/buffer cases are retained.
- Expanded `ScenePathEditCommandsTests`: Object Mode entry, empty/multiple selection, cancellation,
  binding availability and user overrides.
- Added `SceneCurvedArrowTests`: 120-degree exact arc, buffered Bezier midpoint with unequal atom
  radii, vacancy axis/bindings, atom following, and invalid input.
- Added `GlPathShadingTests`: bond-strength specular highlight and an atom-surface occluder for
  opaque/transparent paths, with an always-on-top control.
- Updated the old Butt/Square cap regression to require closed boundaries for all cap types,
  using the existing position-based check to allow duplicated vertices at hard normal boundaries.
- Project regeneration succeeded with `DS_TOOLSET=msc-v143`; all new source/test files are present
  in the generated Visual Studio projects. Premake was invoked directly because the wrapper runs
  `git submodule update --init --recursive --force`, conflicting with the task's Vendor/private
  repository boundary.
- A tree-sitter syntax scan found no new errors across the changed C++ implementations/tests;
  pre-existing grammar limitations were compared against HEAD. `git diff --check` is clean.
- No compilation or executable test run was performed during initial implementation. The caller's
  later build results and the diagnostic run of that existing binary are recorded below.

Suggested focused test filter after the caller builds:

```text
PathRibbonTailTests.*:PathLoopCutTests.*:SceneCurvedArrowTests.*:ScenePathEditCommandsTests.*:PathStrokeMesherTests.ButtSquareAndRoundCapsCloseTheTube:GlTest.PathMaterialUsesTheBondSpecularStrength:GlTest.OpaqueAndTransparentPathsStartingAtAnAtomCentreAreOccludedByItsSphere
```

Also run the existing PathStrokeMesher, PathDecorationBevel, PathSolidCrossSection, and GL path
suites, then the full suite.

## Caller build follow-up

The caller reported a successful Release compilation, 1,205 passing tests and three failures.
No original expectation was removed or relaxed in the fixes below.

- `DecorationMatrixChecksClosedSurfacesAndHandoffFrame`: the new sharp fallback ear-clipped
  four-sided handoff strips. The two faces at each failing handoff fold in projection and are
  not simple polygons, so that algorithm rejected them entirely, leaving eight boundary edges
  open. The sharp fallback again emits the original two triangles for source quads (and one for
  triangles), while keeping ear clipping for larger concave faces. A regression uses the four
  positions from the caller's log and requires both triangles and all four boundary edges.
- `ShadeSmoothAveragesCoincidentBevelNormalsWithoutMovingTheMesh`: strips had smoothing groups,
  but adjoining corner patches remained sharp. Shade Smooth now shares one group over the bevel
  strips and corner patches; the main faces and caps remain in separate groups. Triangles made
  parallel to a terminal cap by the cap constraint remain hard. The original coincident-normal
  equality and unchanged-position assertions are retained.
- `ArcTRisesWithTheShaftAndCarriesTheGradientColour`: newly duplicated start-cap vertices were
  appended after all shaft rings, taking `arcT` back from 1 to 0. The start cap is now emitted
  immediately after the first boundary ring. Stitching tracks the actual previous ring index,
  rather than assuming contiguous rings around the inserted cap vertices. The strict ordering,
  gradient and dash-coordinate checks now cover Butt, Square and Round caps.
- Removed the unused displacement-arrow `outlineModeLocation` query. It was already unused in
  HEAD; moving the path pass did not make it unused. Other outline uniform uses are unchanged.

Validation: syntax scan and `git diff --check` pass, with only the same existing tree-sitter
limitations in `AddFace` and `GLAPIENTRY`. The caller's existing Release binary was run for the
closure test to inspect its complete pre-fix failure trace, saved to
`build/test-artifacts/task68-closure-before.log`. This was diagnosis of the old binary, not
validation of the new source. No build was performed; the caller must rebuild and run the tests.

Focused filter for that rebuild:

```text
PathRibbonTailTests.*:PathStrokeMesherTests.ShadeSmoothAveragesCoincidentBevelNormalsWithoutMovingTheMesh:PathStrokeMesherTests.DecorationMatrixChecksClosedSurfacesAndHandoffFrame:PathStrokeMesherTests.ArcTRisesWithTheShaftAndCarriesTheGradientColour
```

Files edited in this follow-up: `src/Renderer/Path/PathStrokeMesher.cpp`,
`PathSolidMesher.cpp`, `PathSolidBeveler.cpp`, `PathSolidBevelGeometry.cpp`,
`PathSolidBevelGeometry.hpp`, `src/Renderer/OpenGl/OpenGlRendererBackend.cpp`,
`tests/Renderer/Path/PathRibbonTailTests.cpp`, `PathStrokeMesherTests.cpp`, and this report.
Graph metadata is refreshed through the same serial AST update engine used above; project
regeneration is unnecessary because no source/test files were added.

## Files changed by this task

Other agents' edits in the shared tree are excluded from this list.

```text
src/Presentation/Panels/SceneFreePathCreation.cpp (new)
src/Presentation/Panels/ScenePathOperations.cpp
src/Presentation/Panels/ScenePathOperations.hpp
src/Presentation/Panels/ScenePathEditCommands.cpp
src/Presentation/Panels/ViewportPathInsert.cpp
src/Renderer/OpenGl/OpenGlPathRenderer.cpp
src/Renderer/OpenGl/OpenGlRendererBackend.cpp
src/Renderer/Path/PathDecorationMesher.cpp
src/Renderer/Path/PathDecorationMesher.hpp
src/Renderer/Path/PathEditSession.cpp
src/Renderer/Path/PathPicking.cpp
src/Renderer/Path/PathPicking.hpp
src/Renderer/Path/PathSolidBevelGeometry.cpp
src/Renderer/Path/PathSolidBevelGeometry.hpp
src/Renderer/Path/PathSolidBeveler.cpp
src/Renderer/Path/PathSolidMesher.cpp
src/Renderer/Path/PathSolidMesher.hpp
src/Renderer/Path/PathStrokeMesher.cpp
src/Renderer/Path/PathStrokeMesher.hpp
src/Renderer/Path/PathTopology.cpp
src/Renderer/Path/PathTopology.hpp
tests/Presentation/Panels/SceneCurvedArrowTests.cpp (new)
tests/Presentation/Panels/ScenePathEditCommandsTests.cpp
tests/Renderer/Gl/GlPathShadingTests.cpp (new)
tests/Renderer/Path/PathRibbonTailTests.cpp (new)
tests/Renderer/Path/PathLoopCutTests.cpp
tests/Renderer/Path/PathStrokeMesherTests.cpp
docs/work/project/tasks/68-path-round-2-report.md (new)
```

Generated project outputs: `build/generated/vs2022/DefectStudio.vcxproj`, its `.filters`,
`DefectStudioTests.vcxproj`, and its `.filters`.

Graph refresh: `graphify update .` was attempted and failed because the Windows sandbox denies
creation of multiprocessing pipes. The same AST update engine succeeded with serial extraction
of changed files, preserving the remaining graph. The final refresh contains 108,747 nodes,
223,108 edges and 3,151 communities.
It refreshed `graphify-out/graph.json`, `GRAPH_REPORT.md`, the labels/root markers and extraction
cache, and created the engine's backup of the previous curated graph. HTML visualization was
skipped by the engine's 5,000-node limit. `graphify reflect --if-stale` also refreshed
`graphify-out/reflections/LESSONS.md` during navigation. The final scoped refresh succeeded and
covers the two small review edits made after the first successful update.
