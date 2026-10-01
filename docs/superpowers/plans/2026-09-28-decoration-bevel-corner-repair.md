# Decoration Bevel Corner Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make thick `Flat` paths bevel the true silhouette of all eight endpoint decorations, including inward fillets at reflex shoulders and locally limited bevels at acute tips, while generating one deterministic visual gallery for review.

**Architecture:** Keep the shared `ThickFlatMesh` route and move its topology analysis out of `PathSolidBeveler.cpp`. The analysis produces signed convex/reflex/smooth corner data plus a safe radius per source vertex. Face insets, edge strips, and triangular corner lattices consume that single result. Structurally unsupported decorations stay sharp locally and never fall back to the legacy swept-decoration bevel.

**Tech Stack:** C++23, GLM, GoogleTest, OpenGL off-screen test harness, premake5, MSBuild.

**Spec:** `docs/superpowers/specs/2026-09-28-decoration-bevel-corners-design.md`

**Execution status:** In progress — Task 4 (re-entrant profiles and patches). Checkpoints: Task 1 `1e0a4cc`, Task 2 `c509917`, Task 3 `4379042`.

## Global Constraints

- Renderer code remains exception-free and does not introduce raw pointers.
- Do not change serialized `PathStrokeStyle` data or the eight contour definitions.
- Preserve the current output for `ribbonBevel == 0`, `StrokeProfile::Round`, and zero-thickness ribbons.
- Classify corners from geometry; do not branch on `PathDecorationKind` inside the beveller.
- Keep the fixed radial directions already covered by `BevelShapeMovesCornerVerticesAlongSphericalNormals`.
- Reuse exact edge-profile boundary vertices in vertex patches.
- Validate every completed component before appending it; unsupported components remain sharp.
- Keep new production `.cpp` files near 500 lines and reduce `PathSolidBeveler.cpp` while extracting its topology code.
- Stage only files named by the current task. Before each commit run `git diff --cached --name-only` so unrelated user work is not included.
- Treat the current five `PathStrokeMesherTests` failures as the starting baseline, not as acceptable final failures.

---

### Task 1: Add silhouette-aware test oracles and reproduce the arrow failures

**Files:**
- Create: `tests/Renderer/Path/PathDecorationBevelTests.cpp`

**Interfaces:**
- Consumes: `BuildDecorationContour` and `BuildStroke`.
- Produces: reusable test-only checks for finite data, two-manifold closure, projected point-in-polygon containment, and outward triangle winding.

- [x] Add a fixture that builds a straight thick `Flat` path with the same filled decoration at both endpoints. Keep the requested bevel, bevel segments, and bevel shape as parameters:

```cpp
PathStrokeStyle DecoratedStyle(const PathDecorationKind kind, const float bevel,
	const std::uint32_t segments, const float shape)
{
	PathStrokeStyle style;
	style.profile = StrokeProfile::Flat;
	style.width = 0.6f;
	style.ribbonThickness = 0.32f;
	style.ribbonBevel = bevel;
	style.ribbonBevelSegments = segments;
	style.ribbonBevelShape = shape;
	style.startDecoration = {.kind = kind, .filled = true};
	style.endDecoration = {.kind = kind, .filled = true};
	return style;
}
```

- [x] Add `UndirectedEdgeUseCounts`, `ExpectFiniteMesh`, `ExpectClosedMesh`, and `ExpectOutwardWinding` helpers. Count edges by welded position rather than emitted vertex index because render vertices intentionally split normals.
- [x] Add a 2D winding-number helper over the sharp decoration outline and `ExpectInsideSharpSilhouette`. Treat a point within the scale-relative boundary epsilon as inside.
- [x] Add `ArrowReflexShouldersStayInsideTheSharpSilhouette` with bevel `0.04`, four segments, and shape `0.5`. Assert the complete mesh is finite and closed, and every decoration vertex projects inside the corresponding sharp outline.
- [x] Add `ExcessiveArrowBevelLocallyClampsTheTip` with bevel larger than half the Arrow tip's adjacent edge. Assert there are no projected points outside the sharp outline.
- [x] Run the two tests and confirm RED specifically at the shoulder/tip containment assertions:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' 'build\generated\vs2022\DefectStudioTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m:2 /nr:false /v:minimal
& 'build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe' --gtest_filter='PathDecorationBevelTests.ArrowReflexShouldersStayInsideTheSharpSilhouette:PathDecorationBevelTests.ExcessiveArrowBevelLocallyClampsTheTip'
```

- [x] Commit the regression tests only:

```powershell
git add tests/Renderer/Path/PathDecorationBevelTests.cpp
git diff --cached --name-only
git commit -m "test: cover decoration bevel reflex and acute corners"
```

### Task 2: Extract manifold topology and signed corner analysis

**Files:**
- Create: `src/Renderer/Path/PathSolidBevelTopology.hpp`
- Create: `src/Renderer/Path/PathSolidBevelTopology.cpp`
- Modify: `src/Renderer/Path/PathSolidBeveler.cpp`
- Modify: `tests/Renderer/Path/PathDecorationBevelTests.cpp`

**Interfaces:**
- Produces: `BuildThickFlatBevelTopology(const ThickFlatMesh&, std::span<const glm::dvec3>, double, ThickFlatBevelTopology&)`.
- Consumes: one normal per source face and the globally capped requested radius.

- [x] Add `SquareCornerTurnProducesConvexPatch`, `ArrowShoulderTurnProducesReentrantPatch`, and `CircleShallowTurnsRemainAContinuousChain`. Exercise the real extracted topology with complete prism meshes; do not add a test-only classifier.
- [x] Define the shared internal topology types:

```cpp
enum class ThickFlatBevelTurn : std::uint8_t
{
	Convex,
	Reflex,
	Smooth,
	Unsupported,
};

struct ThickFlatBevelIncident
{
	std::size_t face = 0u;
	std::size_t corner = 0u;
};

struct ThickFlatBevelEdge
{
	std::pair<std::uint32_t, std::uint32_t> key{};
	std::vector<ThickFlatBevelIncident> incidents;
	ThickFlatBevelTurn turn = ThickFlatBevelTurn::Unsupported;
	bool bevel = false;
};

struct ThickFlatBevelVertex
{
	std::vector<std::size_t> orderedEdges;
	ThickFlatBevelTurn turn = ThickFlatBevelTurn::Unsupported;
	double radius = 0.0;
};

struct ThickFlatBevelTopology
{
	std::vector<ThickFlatBevelEdge> edges;
	std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> edgeByKey;
	std::map<std::uint32_t, ThickFlatBevelVertex> vertices;
	std::vector<std::vector<ThickFlatBevelTurn>> faceCorners;
};
```

- [x] Move `EdgeKey`, incident-edge construction, two-manifold validation, face transitions, and cyclic edge ordering from `PathSolidBeveler.cpp` into `PathSolidBevelTopology.cpp`. Preserve the existing cuboid behavior before adding signs.
- [x] Classify every face corner with its oriented signed turn and store it in `faceCorners`. `InsetCorner` needs this per-face result: a reflex source vertex alone is insufficient because each of its incident faces sees a different local turn. Use a scale-relative angular epsilon, not exact collinearity:

```cpp
const glm::dvec3 incoming = SafeNormal(point - previous, glm::dvec3(0.0));
const glm::dvec3 outgoing = SafeNormal(next - point, glm::dvec3(0.0));
const double sine = glm::dot(faceNormal, glm::cross(incoming, outgoing));
const double cosine = glm::clamp(glm::dot(incoming, outgoing), -1.0, 1.0);
const double turn = std::atan2(sine, cosine);
```

- [x] Classify each bevel edge from its directed incident face winding and signed dihedral. Combine its incident turns when classifying a source vertex. Any inconsistent or non-manifold combination becomes `Unsupported`.
- [x] Treat outline turns below 15 degrees as `Smooth`; suppressing their independent strips and poles remains in Task 5.
- [x] Rewire `PathSolidBeveler.cpp` to consume `ThickFlatBevelTopology` instead of rebuilding adjacency. The file fell from roughly 600 to 527 lines; later emission extraction will take it below 500.
- [x] Regenerate the Visual Studio project for the new production and test sources, then run the cuboid and new classification tests:

```powershell
& 'Vendor\Binaries\Premake\Windows\premake5.exe' vs2022
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' 'build\generated\vs2022\DefectStudioTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m:2 /nr:false /v:minimal
& 'build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe' --gtest_filter='PathStrokeMesherTests.BevelledFlatCuboidNeverLeavesItsOriginalBounds:PathStrokeMesherTests.BevelledCuboidCornerUsesA2dPatchInsteadOfOneFanPole:PathDecorationBevelTests.SquareCornerTurnProducesConvexPatch:PathDecorationBevelTests.ArrowShoulderTurnProducesReentrantPatch:PathDecorationBevelTests.CircleShallowTurnsRemainAContinuousChain'
```

- [x] Commit the extraction and signed classification:

```powershell
git add src/Renderer/Path/PathSolidBevelTopology.hpp src/Renderer/Path/PathSolidBevelTopology.cpp src/Renderer/Path/PathSolidBeveler.cpp tests/Renderer/Path/PathDecorationBevelTests.cpp
git diff --cached --name-only
git commit -m "refactor: analyze signed solid bevel topology"
```

### Task 3: Limit bevel radius at every source vertex

**Files:**
- Modify: `src/Renderer/Path/PathSolidBevelTopology.cpp`
- Modify: `src/Renderer/Path/PathSolidBeveler.cpp`
- Modify: `tests/Renderer/Path/PathDecorationBevelTests.cpp`

**Interfaces:**
- Changes: `ThickFlatBevelVertex::radius` from the global request to the smallest safe incident-face radius.
- Preserves: radial direction for every profile sample as shape changes.

- [x] Add a direct topology assertion with requested radius `1.0`; each tested corner must converge to its hand-derived cap instead of retaining the excessive request.
- [x] Cover representative `Arrow`, `Diamond`, `Kite`, and short-axis `Bar` corners so the solution cannot be Arrow-specific.
- [x] For every non-smooth incident face corner, calculate the smaller geometric corner angle and cap the radius so an inset advances no farther than 45 percent of either adjacent usable edge:

```cpp
const double cornerAngle = std::acos(glm::clamp(glm::dot(toPrevious, toNext), -1.0, 1.0));
const double maxAdvance = 0.45 * std::min(previousLength, nextLength);
const double faceRadius = maxAdvance * std::tan(0.5 * cornerAngle);
vertex.radius = std::min(vertex.radius, faceRadius);
```

- [x] Reject non-finite, degenerate, or below-tolerance radii by marking only that local corner sharp. Do not abandon a valid shaft bevel.
- [x] Pass endpoint radii into edge-profile generation. Permit a strip to taper linearly when its source vertices have different safe radii.
- [x] Pass the source vertex radius into `InsetCorner` and the triangular lattice. Keep its circular reference directions fixed while `ribbonBevelShape` changes only radial distance.
- [x] Run the acute-corner topology test plus `BevelShapeMovesCornerVerticesAlongSphericalNormals`; confirm GREEN. The end-to-end Arrow containment assertion remains RED until the reflex patch in Task 4:

```powershell
& 'build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe' --gtest_filter='PathDecorationBevelTests.Excessive*BevelLocallyClamps*:PathStrokeMesherTests.BevelShapeMovesCornerVerticesAlongSphericalNormals'
```

- [x] Commit the local-radius change:

```powershell
git add src/Renderer/Path/PathSolidBevelTopology.cpp src/Renderer/Path/PathSolidBeveler.cpp tests/Renderer/Path/PathDecorationBevelTests.cpp
git diff --cached --name-only
git commit -m "fix: clamp bevel radius at acute decoration corners"
```

### Task 4: Emit re-entrant edge profiles and reflex corner patches

**Files:**
- Modify: `src/Renderer/Path/PathSolidBeveler.cpp`
- Modify: `tests/Renderer/Path/PathDecorationBevelTests.cpp`

**Interfaces:**
- Consumes: signed `ThickFlatBevelTurn` and locally capped endpoint radii.
- Produces: convex or re-entrant strips and triangular corner lattices sharing the same boundary samples.

- [ ] Add `ArrowReflexShouldersMoveTowardTheReentrantCentre`. Identify shoulder neighbourhoods from the sharp contour and assert bevel samples lie on the material-removal side of both incident outline edges.
- [ ] Add a winding regression using both start and end Arrow decorations. This catches sign reversal caused by endpoint orientation.
- [ ] Build an oriented profile frame from the directed source edge, the first incident face normal, and the signed dihedral. Reverse the radial frame for `Reflex`; do not reverse sample order independently.
- [ ] Make `EmitTrihedralVertexPatch` accept the source vertex turn and local radius. For reflex corners, orient its reference sphere toward the re-entrant centre:

```cpp
const double orientation = turn == ThickFlatBevelTurn::Reflex ? -1.0 : 1.0;
const std::array<double, 3u> direction = {firstWeight, secondWeight, thirdWeight};
const double scale = RadialProfileScale(direction, shape);
glm::dvec3 position = centre;
for (std::size_t index = 0u; index < direction.size(); ++index)
	position += orientation * faceNormals[index] * localRadius * direction[index] * scale;
```

- [ ] Reuse exact strip-boundary vertex positions in the patch. Before emitting each triangle, compare its geometric normal with the expected outward solid direction and swap the final two corners when needed.
- [ ] If a corner is `Smooth` or `Unsupported`, emit no synthetic pole. Close the local region with the sharp source corner only when that preserves two-manifold topology.
- [ ] Run all focused decoration tests and the existing closure/winding tests:

```powershell
& 'build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe' --gtest_filter='PathDecorationBevelTests.*:PathStrokeMesherTests.BevelledFlatDecoratedStrokeIsOneClosedSurface:PathStrokeMesherTests.DecorationMatrixChecksClosedSurfacesAndHandoffFrame'
```

- [ ] Build and launch the Debug application, reproduce the Arrow from `docs/figs/bevel2.png` through `bevel4.png`, and stop for visual inspection before continuing. Confirm shoulders bend inward and the tip remains bounded.
- [ ] Commit the oriented reflex patch:

```powershell
git add src/Renderer/Path/PathSolidBeveler.cpp tests/Renderer/Path/PathDecorationBevelTests.cpp
git diff --cached --name-only
git commit -m "fix: orient bevel patches at reflex decoration corners"
```

### Task 5: Handle smooth sampled contours and structural fallback for all decorations

**Files:**
- Modify: `src/Renderer/Path/PathSolidMesher.hpp`
- Modify: `src/Renderer/Path/PathSolidMesher.cpp`
- Modify: `src/Renderer/Path/PathStrokeMesher.cpp`
- Modify: `src/Renderer/Path/PathSolidBevelTopology.cpp`
- Modify: `tests/Renderer/Path/PathDecorationBevelTests.cpp`

**Interfaces:**
- Changes: solid-bevel eligibility from one whole-path boolean to component-level structural eligibility.
- Guarantees: an unsupported decoration is sharp locally and never receives the legacy swept cross-section bevel.

- [ ] Add a parameterized matrix covering `Arrow`, `Stealth`, `Latex`, `Bar`, `Circle`, `Square`, `Diamond`, and `Kite`; start and end orientation; shapes `0.25`, `0.5`, `0.75`; normal and excessive requested bevel.
- [ ] For supported components assert finite data, closure, winding, and silhouette containment. For `Stealth`, assert either a valid manifold bevel or the exact sharp local decoration; both endpoints must remain present.
- [ ] Add `CircleAndLatexDoNotEmitOneCornerBulgePerSample`. Compare positions and connected rings rather than a driver-dependent rendered image.
- [ ] Replace `UsesThickFlatSolidBevel` with an eligibility result that distinguishes the shaft, start decoration, and end decoration:

```cpp
struct ThickFlatSolidBevelEligibility
{
	bool shaft = false;
	bool startDecoration = false;
	bool endDecoration = false;
};

[[nodiscard]] ThickFlatSolidBevelEligibility ResolveThickFlatSolidBevelEligibility(
	const PathStrokeStyle &style, const DecorationContour &start, const DecorationContour &end);
```

- [ ] Add a `bevelOutline` parameter to `AppendAttachedThickFlatDecoration`. Keep positive-thickness `Flat` geometry on the shared solid path whenever the shaft is eligible. For a closed but locally unsupported decoration, set all of its exterior `bevelEdges` flags false while retaining the shared attachment topology.
- [ ] For an open or hollow decoration that cannot enter `ThickFlatMesh`, append it through `AppendDecoration` with a local style copy whose `ribbonBevel` is zero. Keep the eligible shaft and opposite decoration on the solid route, and preserve their existing mesh ranges when combining the outputs.
- [ ] Remove scale-relative duplicate contour samples before solid adjacency. Collapse shallow same-face turns into their neighbours so Circle and Latex form a continuous edge chain without one spherical patch per sample.
- [ ] Add an explicit regression proving an unsupported decoration has no diagonal swept chamfer across its front/back face.
- [ ] Run the complete new matrix and the original `PathStrokeMesherTests` suite. Investigate every failure; do not update expectations to accept spikes, open meshes, or the old decoration fallback:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' 'build\generated\vs2022\DefectStudioTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m:2 /nr:false /v:minimal
& 'build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe' --gtest_filter='PathDecorationBevelTests.*:PathStrokeMesherTests.*'
```

- [ ] Commit the component fallback and eight-decoration matrix:

```powershell
git add src/Renderer/Path/PathSolidMesher.hpp src/Renderer/Path/PathSolidMesher.cpp src/Renderer/Path/PathStrokeMesher.cpp src/Renderer/Path/PathSolidBevelTopology.cpp tests/Renderer/Path/PathDecorationBevelTests.cpp
git diff --cached --name-only
git commit -m "fix: constrain bevel applicability per decoration component"
```

### Task 6: Add the automatic shaded and mesh visual galleries

**Files:**
- Modify: `tests/Renderer/Gl/GlPathRenderTests.cpp`

**Interfaces:**
- Reuses: `RenderPath`, `WriteVisualArtifact`, and the selected-path mesh overlay.
- Produces: `build/test-artifacts/path-decoration-bevel-gallery.png` and `build/test-artifacts/path-decoration-bevel-gallery-mesh.png`.

- [ ] Add `DecorationKinds()` returning the eight non-`None` kinds in the spec order, and a `PopulateDecorationBevelGallery(PathSystem&)` helper. Place eight rows by three shape columns; use the same kind at both endpoints in each cell.
- [ ] Configure each cell as a thick `Flat` path with four bevel segments, fixed bevel radius, shape `0.25`, `0.5`, or `0.75`, and `shadeSmooth = true`. Give rows stable distinct colors and keep transforms deterministic.
- [ ] Render a sufficiently large shaded artifact with one camera and background. Assert each of the 24 known screen cells contains non-background pixels.
- [ ] Render the same gallery with all path IDs selected and the existing orange diagnostic mesh overlay. Count orange pixels and assert the count is non-zero.
- [ ] Assert the three shape columns differ in pixels without treating either image as a golden file:

```cpp
EXPECT_GT(NonBackground(shaded, width, height, {0, 0, 0, 255}), 0u);
EXPECT_NE(ColumnHash(shaded, width, height, 0u), ColumnHash(shaded, width, height, 1u));
EXPECT_NE(ColumnHash(shaded, width, height, 1u), ColumnHash(shaded, width, height, 2u));
EXPECT_GT(CountSelectionOrange(mesh, width, height), 0u);
```

- [ ] Run only the gallery test, then inspect both PNG files at original resolution. Verify no outward bubbles, detached spikes, missing rows, or diagonal face chamfers. Record any defect as a failing geometry test before changing production code.
- [ ] Commit the gallery:

```powershell
git add tests/Renderer/Gl/GlPathRenderTests.cpp
git diff --cached --name-only
git commit -m "test: render decoration bevel visual gallery"
```

### Task 7: Final regression, application check, and graph refresh

**Files:**
- Modify only when a failing assertion demonstrates a production defect: files already listed above.
- Refresh generated knowledge graph: `graphify-out/**`.

**Interfaces:**
- Verifies: both build configurations, all renderer tests, visual artifacts, runtime application, and graph consistency.

- [ ] Run the focused Release geometry suites and confirm zero failures:

```powershell
& 'build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe' --gtest_filter='PathDecorationBevelTests.*:PathStrokeMesherTests.*'
```

- [ ] Run the focused OpenGL gallery test and inspect the regenerated artifacts:

```powershell
& 'build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe' --gtest_filter='GlTest.DecorationBevelGalleryRendersAllKindsAndShapes'
```

- [ ] Build and test Release, then Debug, using the repository scripts or the generated test project. The only allowed skips are the two documented pre-existing skips.
- [ ] Launch the Debug application with logging, inspect Arrow first, then all seven remaining decoration kinds. Vary shape across `0.25`, `0.5`, and `0.75`; leave Shade smooth on because it must change normals only.
- [ ] If visual inspection finds an error, first add the smallest failing geometry assertion to `PathDecorationBevelTests.cpp`, then repeat the RED-GREEN cycle in the owning task. Do not patch the gallery or contour definition to hide the symptom.
- [ ] Run repository hygiene and refresh the graph:

```powershell
git diff --check
graphify update .
git status --short
```

- [ ] Review the final diff against every acceptance criterion in the spec: signed reflex behavior, acute radius cap, smooth sampled chains, all eight decorations, structural fallback, no old swept-decoration bevel, and unchanged shade-smooth positions.
- [ ] Commit only the final verified corrections and graph updates, after checking the staged file list:

```powershell
git add src/Renderer/Path/PathSolidMesher.hpp src/Renderer/Path/PathSolidMesher.cpp src/Renderer/Path/PathStrokeMesher.cpp src/Renderer/Path/PathSolidBeveler.cpp src/Renderer/Path/PathSolidBevelTopology.hpp src/Renderer/Path/PathSolidBevelTopology.cpp tests/Renderer/Path/PathDecorationBevelTests.cpp tests/Renderer/Gl/GlPathRenderTests.cpp
git diff --cached --name-only
git commit -m "fix: complete decoration silhouette bevel handling"
```

Keep generated `graphify-out/**` changes unstaged unless `git diff -- graphify-out` proves they were produced by this feature and contain no pre-existing user edits.

## Plan Review

- The first hard oracle is geometry, so visual output cannot silently bless a non-manifold or out-of-silhouette mesh.
- The implementation fixes the shared solid beveller and does not encode Arrow, Circle, or any other decoration kind in production bevel logic.
- Acute limiting, reflex orientation, smooth-chain behavior, and unsupported fallback each have an isolated RED-GREEN step.
- Both endpoint orientations are exercised by using the same decoration at both ends.
- All eight decoration kinds and all three requested bevel shapes appear in the automatic gallery.
- The two visual checkpoints make the iterative review requested by the user explicit.
- Every production type in the snippets matches the existing `ThickFlatMesh` use of `std::uint32_t`, `std::size_t`, `glm::dvec3`, and exception-free `bool` validation.
- No step contains a placeholder or requires a new dependency.
