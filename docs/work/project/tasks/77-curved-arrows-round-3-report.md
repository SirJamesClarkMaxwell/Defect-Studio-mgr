# Task 77 report: continuous curved arrows, C₂ pairs, tilt and selection

Implemented in the shared working tree; no commit. Builds are reserved for the caller.

## Changes and causes

- The radius jump came from the radial-offset clearance branch in `ScenePathCurvedArrow.cpp`.
  Just inside the clearance sphere, the chord was trimmed to its far exit; just outside, trim
  became zero. For a 2 Å orbit, 0.4 Å atom and 1.15 buffer, radiusScale 1.2299 → 1.2301
  changes that trim by 0.796628 Å. This is a branch discontinuity, not slider precision.
- Removed radial endpoint offsets and the sphere-intersection trim override from curved-arrow
  creation. Endpoints now retain ordinary CopyPosition/CopyVacancy bindings, zero offsets and
  the requested surface clearance. RadiusScale scales the sagitta continuously:
  `sweep = 4 atan(radiusScale tan(baseSweep / 4))`. Scale 1 preserves the previous default sweep
  exactly; scale 1.3 increases the shallow bulge by 30%, with endpoints unchanged. Thus the
  existing radius control moves the arc body outward, rather than sending its ends away from
  their atoms. This is a deliberate change to task 74's radial-offset interpretation.
- Non-bond two-end mode uses arrowCount 1–2, default 2. A→B and B→A share the signed sweep
  and bulge to opposite sides. The shared chord-line orientation keeps them opposite after tilt
  too. Atom–atom, atom–vacancy and vacancy–vacancy bindings remain attached. Batch validation,
  rollback, all-path selection and one undo entry reuse the existing creation flow.
- Added Float `tiltDegrees`, Polish label `Nachylenie`, range −180…180°, default 0. It rotates
  the plane normal about the chord after projecting that normal perpendicular to the chord.
  A 90° tilt puts the midpoint one arc height off its original plane without moving either end.
  Nonfinite tilt falls back to 0; finite values clamp to the schema range. Bond mode retains
  rotationDegrees and its existing count 1–6 and sweep caps.
- The redo schema/relevance now exposes count for non-bond pairs and tilt for cycles/pairs.
  A small optional operator maximum callback supplies count maximum 2 for pairs and 6 for
  bond mode. The generic panel caches it against the original input selection alongside the
  hidden-key cache, so creation consuming the selection cannot change the slider's range.
- Click selection and box/circle dispatch treated paths as labels and required pickLabels.
  Removed that gate for paths; labels retain their mask. This makes arrows selectable in the
  default/atom modes and Wszystko (Ctrl+4). Existing path hit-testing already uses the rendered
  cached world polyline and a 4 px padding, so no tolerance increase or second geometry resolver
  was needed. Bound and BondFrame arcs retain the same resolve/tessellate flow as rendering.
- Created curved arrows explicitly set `style.shadeSmooth = true` in both bond and non-bond
  branches. Round shaft and round arrowhead already use radial cross-section normals through
  the same tube vertex/shader flow; no separate head shading flag/change was needed. A live
  visual check remains for the caller.

## Visible redo controls

| Mode | Visible controls |
|---|---|
| Cycle | radiusScale, endGap, curvature, tiltDegrees, decoration, color, strokeWidth |
| Non-bond pair | axisMode, arrowCount (1–2), radiusScale, endGap, curvature, tiltDegrees, decoration, color, strokeWidth |
| Bond | axisMode, radiusRule, radiusFactor, arrowCount (1–6), sweepDegrees, rotationDegrees, decoration, color, strokeWidth |

## Tests and verification

Added `Task77SceneCurvedArrowTests.cpp`:

- 0.01-step sweeps of radiusScale 1.00…1.50, endGap 0…1 and curvature 0.05…1.50 for C₃ and
  non-bond C₂. Evaluate every start, midpoint and end through ResolveNodePositions/EvaluateSegment;
  consecutive world-position changes must stay below 0.05 Å. Uses unequal atom radii.
- Scale 1.3 leaves ends unchanged and increases each cycle's midpoint height by exactly 30%.
- Atom/atom, atom/vacancy and vacancy/vacancy pairs, counts 0/1/2/6 (clamped to 1–2), retained
  binding kinds, selected IDs, opposite bulges at both 0° and 90°, and swapped resolved ends.
- 90° tilt: midpoint height off the original plane and unchanged ends for cycles and pairs.
- Whole tilted-pair undo/redo, selected IDs and restored geometry.
- Box/circle hit tests include visible bound and BondFrame arcs with pickLabels both false and true.

Extended existing tests:

- ScenePathPickingTests: pure perspective/orthographic picks 2 px off a 0.03 Å curved stroke;
  atom-bound endpoints with offscreen authored fallbacks and Free nodes in a translated/rolled
  BondFrame. Zero padding misses at the same zoom, demonstrating the pixel padding is exercised.
- SceneOperatorRegistryTests: exact visible sets, tilt schema/default, pair/bond count maxima,
  vacancy pair relevance.
- OperatorRedoPanelTests: radius and tilt change all cycle midpoints; switching modes refreshes
  visibility; original input selection reaches the maximum callback; pair count/tilt reapply,
  all selected and one undo/redo entry.

Deliberate existing-expectation updates only:

- Default non-bond pair counts change from one to two in SceneCurvedArrowTests and redo tests.
- Redo hidden-key expectations include tilt in bond mode and expose count in two-end mode.
- Radius redo assertions now check midpoint changes, not displaced endpoints.
- Task74's outward-first-arc test explicitly requests count 1 and expects zero binding offsets.
  Its default-scale geometry, atom-clearance and bond-mode expectations remain.

Executed using PowerShell/native commands: owned-file `git diff --check`, conflict-marker,
UTF-8 replacement-character and <500-line C++ checks; independent analytic geometry checks.
The analytic sagitta sweeps had maximum consecutive changes 0.004363 Å (radius),
0.001783 Å (gap), and 0.017227 Å (curvature). At camera distance 10 Å, the perspective stroke's
half-width is 1.086396 px: a cursor 2 px off it needs, and fits within, the existing 4 px padding.
These math/static checks do not execute the C++ implementation.

**No build, C++ test executable, project regeneration or live visual test was run.** The caller
must regenerate the test project for the new source, build, and run:

```text
DefectStudioTests.exe --gtest_filter=Task77SceneCurvedArrowTests.*:Task77SceneCurvedArrowUndoTests.*:SceneCurvedArrowTests.*:SceneCurvedArrowUndoTests.*:Task74SceneCurvedArrowTests.*:OperatorRedoPanelTests.*:SceneOperatorRegistryTests.*:ScenePathPickingTests.*:PathPickingTests.*:ScenePathOperationsTests.*
```

Also check the attached C₃ scene's radius/gap sliders, hBN C₂ count and 90° tilt, thin-stroke
click selection in default and Ctrl+4 modes, box/circle selection, and undo/redo after redo edits.
Previously saved arrows keep their authored geometry; recreate/reapply to use the new controls.
Short chords retain the existing buffer non-inversion clamp, which can limit unattainable clearance.

## Files changed by task 77

1. src/Presentation/Operators/CurvedArrowOperator.cpp
2. src/Presentation/Operators/SceneOperator.hpp
3. src/Presentation/Panels/OperatorRedoPanel.cpp
4. src/Presentation/Panels/OperatorRedoPanel.hpp
5. src/Presentation/Panels/ScenePathCurvedArrow.cpp
6. src/Presentation/Panels/ScenePathCurvedArrow.hpp
7. src/Presentation/Panels/ViewportRegionSelect.cpp
8. src/Presentation/Panels/ViewportScenePathInteraction.cpp
9. src/Renderer/Path/CurvedArrowParameters.hpp
10. tests/Presentation/Operators/SceneOperatorRegistryTests.cpp
11. tests/Presentation/Panels/OperatorRedoPanelTests.cpp
12. tests/Presentation/Panels/SceneCurvedArrowTests.cpp
13. tests/Presentation/Panels/Task74SceneCurvedArrowTests.cpp
14. tests/Presentation/Panels/Task77SceneCurvedArrowTests.cpp (new)
15. tests/Renderer/Path/ScenePathPickingTests.cpp
16. docs/work/project/tasks/77-curved-arrows-round-3-report.md (this report)

No changes by this task to install/users, Vendor, Domain, Core/Undo, RendererLayer.cpp,
ViewportSelection*, ViewportInteraction*, vacancy-add/rendering or orbital-rendering files.
Other tasks are actively editing the shared tree; their changes are preserved and excluded above.

## Knowledge graph

`graphify update .` (AST-only) was run after the code/test edits and exited with code 1:

```text
WARNING: could not scan pytest-cache-files-yly255eu ([WinError 5] access denied)
[graphify watch] Rebuild failed: [WinError 5] Odmowa dostępu
Nothing to update or rebuild failed — check output above.
```

The CLI did not identify the rebuild failure path. graph.json and manifest.json retain their
2026-10-05 timestamps, so the graph was not refreshed. This matches the task 73/74 access-denied
failures. No permission changes or force rebuild were attempted; retry outside this sandbox.
