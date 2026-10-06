# Task 76 implementation report

Implemented in the existing shared working tree. Changes are uncommitted.
No project generation, build, C++ test execution or live GPU/visual verification was performed,
as required by the task. Tasks 77/78's concurrent edits are outside the file list below.

## Behaviour and decisions

### Atom end, thickness and colour

- Atom-vacancy paths now bind both ends to their source centres: CopyPosition buffer 0,
  CopyVacancy buffer 0. Vacancy-vacancy paths retain buffer 0 at both ends.
- This removes the fixed 0.9 atom-radius assumption that exposed a thick tube's rim.
  For example, atom radius 0.15 and tube radius 0.09 give an old rim distance
  sqrt((0.9 * 0.15)^2 + 0.09^2) = 0.16225, outside the atom.
- Audited renderBonds: ordinary bonds currently use radius-aware trimming
  sqrt(max(atomRadius^2 - effectiveTubeRadius^2, 0)), limited to 45% of the bond length
  at each end. They do not literally start at the centres. Centre bindings are used here
  as explicitly requested; the atom depth hides the interior portion.
- Width remains 2 * max(bond.radius, 0.001) * bondRadiusMultiplier, using the first
  structure bond's radius and the existing 0.09 fallback when there are no bonds.
  Regeneration refreshes old saved widths to the current effective bond radius.
- Ordinary bonds' two endpoint colours use a smooth linear mix in bonds.frag, rather than a
  hard split at the midpoint. Generated paths retain the same two-stop linear colour ramp and
  the path shader's matching diffuse/specular lighting.

### Vacancy-side occlusion audit

- Draw order is already correct: opaque atom geometry, depth-tested paths, vacancy markers,
  other overlays, labels, then explicitly AlwaysOnTop paths.
- The two renderScenePaths calls at OpenGlRendererBackend.cpp:808 and :836 are disjoint.
  The first accepts DepthTest paths and the second accepts AlwaysOnTop paths. Generated bonds
  have DepthTest style and are never redrawn in the second pass, including when selected.
- The diagnostic mesh overlay explicitly disables depth only for selected paths when
  showMeshOverlay is enabled. This existing selection behaviour was left intact.
- Filled marker draws already retain depth writes: renderIsosurfaceGpuOverlay defaults
  writeDepth to true, and renderScenePaths restores the previous depth mask. No new depth
  pass or change to the backend's GL_LEQUAL comparison was needed.
- The fill previously stopped at radius r - ringWidth / 2. Between dashed ring segments this
  left an unfilled annulus, through which a tube could remain visible. Fill vertices now reach
  radius r, matching the analytic sphere radius and the visible outline. Dark dashes still
  draw last, at equal spherical depth with LEQUAL.
- Wireframe keeps its empty interior. Its opaque dashes now use the same analytic sphere depth
  as filled markers, instead of falling back to the centre plane. A tube can still be seen
  through the open interior/gaps, as expected for Wireframe.
- Analytic sphere depth remains unchanged. Perspective uses the fragment-to-camera ray;
  orthographic uses the camera-facing normal extracted from the view matrix. Marker centres
  and fragment positions both include sceneOffset.
- A translucent fill necessarily shows some underlying geometry. Merely moving a flat cap
  to the vacancy centre would leave that cap visible through the tint. Generated bonds now
  omit terminal shaft caps altogether, using the existing mesher's cap switches through a
  defaulted capEndpoints argument. Their tube walls remain visible only through the fill
  wherever the sphere surface is in front of them; there is no flat endpoint disc to reveal.
- Other paths keep capEndpoints=true. This change adds no persisted field or format migration.
  The shared isosurface.frag shader was not edited; ordinary orbital/isosurface shader behaviour
  is byte-for-byte unchanged.

Depth behaviour was checked against the primary
[Khronos depth-mask reference](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glDepthMask.xml)
and [depth-function reference](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glDepthFunc.xml).
Context7 was not available in this session; no Vendor sources were opened.

### Regeneration and old projects

Every vacancy-bond menu/property entry routes through AddVacancyBonds, so the shared change
covers RMB Add, Shift+A, toolbar Add and the vacancy properties "Wiązania do sąsiadów" action.

GeneratedVacancyBondPair recognises two-node, one-Line paths with the generated round,
opaque, undashed, undecorated, depth-tested, two-stop gradient style and either
CopyPosition + CopyVacancy or two CopyVacancy bindings. It accepts the historic atom
buffers 0.9/0 and vacancy buffers 1/0, requires zero binding offsets, and recognises reversed
endpoints. Vacancy-vacancy pairs are canonicalised by index. Names are never consulted.

For each requested pair, regeneration replaces the first matching generated path in place,
preserving its object ID, persistKey and name, and then deletes all matching duplicates.
Deletion happens only after the replacement validates. Invalid/nonfinite/nonpositive replacement
widths retain the old paths. Add/edit/delete operations use existing silent path contexts, and
one scene-object snapshot records the complete successful batch for undo/redo.

User paths with different styles, decorations, opacity, endpoint offsets or other buffers are
left alone, even if named "Vacancy bond". Generated bonds for other pairs are left alone.
A manually authored path with exactly the same generated style and bindings is inherently
indistinguishable from a generated bond in the existing file format; this implements the task's
binding-and-style definition without introducing provenance metadata.

Repeated atom selections are deduplicated before applying the batch. Also fixed the existing
"none selected: every vacancy" loop: it previously stopped after inserting the first vacancy
because chosen.empty() became false.

**Upgrade an old project:** open it, select the vacancy (or the desired atom/vacancy pair),
and click "Wiązania do sąsiadów" once. The matching legacy bonds are updated and duplicate
generated paths collapse to one per pair. Repeat for other vacancy sites, or clear the atom
and vacancy selections and use Add -> vacancy bonds to refresh all vacancy neighbour shells.
Save the project afterwards. One Ctrl+Z restores the entire pre-click batch, including old
buffers, widths and duplicates. Explicitly edited user-style paths are intentionally preserved.

## Other path types: findings only

Straight Arrow / Line through selected atoms or vacancies in ScenePathOperations.cpp uses
GetScenePathAtomBuffer() for both endpoints, normally 1.15. These paths intentionally end
outside the spheres and retain their flat caps/decorations. If an authored buffer is reduced
inside a sphere, their endpoint trimming still measures centreline distance, not tube-rim
clearance, so the same thick-tube intersection problem can occur. The shared resolver's
short-path clamp can also reduce the requested clearance.

Curved arrows use atom/vacancy clearance bindings and the same general shaft/decorations
mesher; endpoint clearance alone does not prove that an entire curved tube or arrowhead
misses a sphere. They retain their existing caps and can have analogous intersections at
small gaps, large thickness or on a short path. Task 77 is editing that flow concurrently.
No straight-arrow/line or curved-arrow generator, resolver, selection code or operator was
changed by task 76.

## Tests and verification

Updated/added in existing C++ test files:

- Centre-bound atom and vacancy endpoints, including a radius/thickness combination that made
  the old 0.9-radius rim protrude; vacancy movement leaves the atom endpoint at its centre.
- Effective radius clamp and multiplier; depth-tested generated style.
- Generated tube has no terminal cap normals/triangles; its midpoint colour matches ordinary
  bonds' linear colour mix. The default mesher still produces capped ordinary paths.
- Renamed legacy atom-vacancy paths, reversed duplicate endpoints, repaired buffers/widths,
  retained ID/key/name, repeat generation without growth, and one-step undo/redo of the batch.
- Reversed legacy vacancy-vacancy pairs and duplicates, including one-step undo/redo.
- Eight non-generated user-style/binding variants and unrelated generated pairs remain intact.
- Failed zero-width regeneration leaves old paths intact and adds no undo entry.
- No selection visits every vacancy, and filled marker geometry reaches the outer outline.

Executed with PowerShell:

- 144 analytic sphere assertions (48 ray cases, three assertions each): centre and rim samples,
  radii 0.2/0.5/1.1, origin and translated centres, perspective and orthographic rays. Verified
  points lie on the sphere, use the front intersection and sit in front of the centre plane.
- 12 source invariants covering centre endpoints, pair deduplication, cap dispatch/defaults,
  marker radius, disjoint path passes, selected-only mesh overlay, LEQUAL and camera-ray modes.
- Strict UTF-8, conflict-marker and under-500-line checks for the changed C++ files.
- git diff --check for this task's tracked changes.

**C++ tests are not compiled or run. These checks do not prove final GPU pixels.**
No pure C++ analytic sphere helper exists: the depth function lives in GLSL and was not changed.

After the caller builds, run:

    DefectStudioTests.exe --gtest_filter=ViewportVacancyAddTests.*:VacancyBondRegenerationTests.*:VacancyMarkerGeometryTests.*:PathStrokeMesherTests.*

Then run the full suite. The known pre-existing five PathStrokeMesherTests bevel failures
mentioned in task 60 remain the baseline to compare against. No new .cpp test/application file
was added; VacancyBond.hpp is a new included header. Regenerate projects if the caller needs it
listed in the IDE; project generation was not run here.

## Precise visual check for the caller

1. Reproduce the attached diamond/NV-like view with the top N, all three C neighbours and defect
   axes. Create fresh bonds, then separately upgrade a saved old project with one generator click.
   Inspect both selected and deselected bonds with the diagnostic mesh overlay off.
2. Orbit through front, back, left/right and upper/lower oblique views. Look nearly along each
   tube axis from both ends; include a grazing view of the vacancy rim. The N sphere must hide
   its tube junction without the light ellipse shown in the screenshot.
3. Repeat every angle in perspective and orthographic. Zoom in/out so the 1.5-pixel ring-width
   floor is exercised, and include a nonzero scene offset.
4. Solid marker: no interior tube wall or end disc may appear. Ghost/translucent marker at
   opacity 0.2, 0.5 and 0.8: visible interior walls must be blended through the marker colour,
   with no flat endpoint ellipse. Check all dashed gaps and the lower C3 junction specifically.
5. Test both a continuous ring and a dashed ring, narrow/wide rings, and Wireframe. Wireframe's
   open interior remains visible, while opaque dashes occlude the tubes at spherical depth.
6. Repeat with bond-radius multipliers 0.5, 1 and 2.5 and compare thickness/colour with adjacent
   ordinary bonds. Use sensible marker/atom radii larger than the tube radius; a tube physically
   wider than its containing sphere cannot be hidden by that sphere.
7. Include a vacancy-vacancy bond and multiple vacancies. Regenerate twice: object count must
   stay stable. Undo once: old widths/buffers/duplicate objects return; redo repairs them again.
8. Show ordinary orbitals/isosurfaces after the markers to verify that vacancy uniforms reset.
   Explicit AlwaysOnTop user paths and selected diagnostic mesh overlays retain their existing
   overlay behaviour.

## Files changed by task 76

- src/Presentation/Panels/ViewportVacancyAdd.cpp
- src/Presentation/Panels/ViewportVacancyAdd.hpp
- src/Renderer/OpenGl/OpenGlPathRenderer.cpp
- src/Renderer/OpenGl/OpenGlVacancyRenderer.cpp
- src/Renderer/Path/PathStrokeMesher.cpp
- src/Renderer/Path/PathStrokeMesher.hpp
- src/Renderer/Path/VacancyBond.hpp (new)
- src/Renderer/Scene/VacancyMarkerGeometry.cpp
- src/Renderer/Scene/VacancyMarkerGeometry.hpp
- tests/Presentation/Panels/ViewportVacancyAddTests.cpp
- tests/Renderer/Scene/VacancyMarkerGeometryTests.cpp
- docs/work/project/tasks/76-vacancy-bonds-for-real-report.md (this report)

## Knowledge graph

graphify update . was invoked after the code edits (AST-only, no API cost). It exited with code 1:

    [graphify watch] Rebuild failed: [WinError 5] Odmowa dostępu
    Nothing to update or rebuild failed — check output above.

It also warned that pytest-cache-files-yly255eu could not be scanned because of access denial.
The existing graph.json was not refreshed, and git status shows no changed graphify-out files.
This is the same sandbox access limitation recorded in the task 74 report. Retry the graph refresh
outside this restricted sandbox. No API extraction cost was incurred.
