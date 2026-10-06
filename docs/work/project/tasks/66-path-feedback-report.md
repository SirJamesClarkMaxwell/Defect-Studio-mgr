# Task 66 implementation report

Implemented on the existing `task/65-feedback-round` branch. Regression tests were written before the corresponding implementation changes. No build, test execution, project regeneration, or commit was performed, per the caller's working arrangement.

## 1. Selected tube stripes

The selection-coloured mesh diagnostic overlay was enabled by default (`showPathMeshOverlay = true`). It draws the tube's longitudinal edges and cap rings, matching the reported stripes and dots. The existing silhouette pass already front-culls the expanded shell and applies its outline depth bias. Changed the overlay default to false; the existing UI checkbox still enables it explicitly.

Files: `src/Renderer/RendererWindowState.hpp`, `tests/Renderer/Gl/GlPathRenderTests.cpp`, `tests/Presentation/Panels/ScenePathEditorWidgetTests.cpp`, `tests/Presentation/Panels/ScenePathEditCommandsTests.cpp`.

Added tests:

- `GlTest.DefaultSelectedTubeHasASilhouetteWithoutOrangeInteriorStripes`
- `ScenePathEditorWidgetTests.MeshOverlayDefaultsOff`
- `ScenePathEditCommandsTests.MeshOverlayIsOptIn`

## 2. Bevel parts and decoration artefacts

Added `PathBevelParts::{Both,Shaft,Decorations}` and one `PathStrokeStyle::ribbonBevelParts` field, defaulting to Both. The optional YAML key is `ribbon_bevel_parts`, with values `Both`, `Shaft`, and `Decorations`; missing keys retain the old default. The style editor supports the choice and mixed selections.

The beveler treated coplanar contour seams as bevel edges, shifted straight-chain profile anchors as if they were corners, and constructed oblique corner interiors using perpendicular-face assumptions. Those mechanisms could create collapsed strips, midpoint bumps, and protruding corner patches. Coplanar seams now stay sharp, straight-chain anchors stay at their contour vertices, and oblique corner interiors use the actual boundary profiles. Perpendicular box corners retain the existing spherical construction.

Selective beveling preserves face ownership and enables only edges between selected faces. It tapers the bevel to zero at the shared boundary so the excluded part keeps its geometry and the solid remains connected. Separate shaft/decoration meshing also honours the setting.

Files: `src/IO/SceneObjectsIO.hpp`, `src/IO/SceneObjectsYaml.cpp`, `src/Renderer/Path/PathStyle.hpp`, `src/Renderer/Path/PathSolidBeveler.cpp`, `src/Renderer/Path/PathSolidMesher.cpp`, `src/Renderer/Path/PathSolidMesher.hpp`, `src/Renderer/Path/PathStrokeMesher.cpp`, `src/Renderer/Scene/ScenePathPersistence.cpp`, `src/Presentation/Panels/ScenePathEditorWidget.cpp`, `src/Presentation/Panels/ScenePathEditorWidget.hpp`, and the three test files below.

Added tests:

- `PathDecorationBevelTests.ArrowSquareAndBarBevelStayClosedInsideSharpBounds`: Arrow/Square/Bar, segments 1/4/16, shapes 0/0.5/1; finite geometry, sharp bounds, closure, winding, signed volume, and Arrow silhouette.
- `PathDecorationBevelTests.BevelPartsKeepTheOtherPartSharpAndTheSolidClosed`
- `ScenePathPersistenceTests.BevelPartsRoundTripAndMissingKeyDefaultsToBoth`
- `ScenePathEditorWidgetTests.BevelPartsApplyAndResolveMixedSelections`

The existing decoration closure helper additionally checks opposing shared-edge traversal.

## 3. Ctrl+R modal insertion

The existing single-node split operation provided line interpolation, de Casteljau subdivision, and arc subdivision, but had no hovered-segment modal interface or multiple-insert operation. Extended that operation with atomic `InsertNodes`, and added `InsertScenePathNodes` through the existing `ApplyPathEdit` snapshot/undo mechanism.

Registered `renderer.path_edit.loop_cut` and a contextual Ctrl+R fallback binding. Existing user bindings for this action, including disabled/remapped bindings, suppress the fallback. The viewport locks the hovered segment, draws preview nodes, changes the count with the wheel (1..32), commits on left click/Enter, and cancels on right click/Escape. Preview changes neither the path nor undo history. Inserted nodes become selected in node mode after commit.

Splitting uses displayed positions and handles, then converts them back to authored coordinates. This preserves displayed circular arcs under nonuniform scaling and retains existing bindings. Buffered atom-bound endpoints require an offset adjustment because their buffer direction/clamp depends on the nearest node.

Files: `src/Presentation/EditorLayerMenus.cpp`, `src/Presentation/Panels/ScenePathEditCommands.cpp`, `src/Presentation/Panels/ScenePathEditCommands.hpp`, `src/Presentation/Panels/ViewportInteraction.cpp`, `src/Presentation/Panels/ViewportPathInsert.cpp`, `src/Presentation/Panels/ViewportPathInsert.hpp`, `src/Renderer/Path/PathCommands.hpp`, `src/Renderer/Path/PathInsertCommands.cpp`, `src/Renderer/Path/PathEditSession.cpp`, `src/Renderer/Path/PathEditSession.hpp`, `src/Renderer/Path/PathPicking.cpp`, `src/Renderer/Path/PathPicking.hpp`, `src/Renderer/Path/PathTopology.cpp`, `src/Renderer/Path/PathTopology.hpp`, and the two test files below.

Added tests in `tests/Renderer/Path/PathLoopCutTests.cpp`:

- `PathLoopCutTests.EvenSplitsPreserveLineCubicAndArcShape`
- `PathLoopCutTests.SegmentOnlyPickingIgnoresEndpointMarkers`
- `PathLoopCutTests.InvalidCountOrSegmentLeavesPathUntouched`
- `PathLoopCutTests.CommitHasOneUndoAndRestoresTheOriginalCurve`
- `PathLoopCutTests.BufferedBoundEndpointKeepsItsDisplayedPositionAndBinding`
- `PathLoopCutTests.TransformedArcInsertPreservesTheDisplayedCircle`
- `PathLoopCutTests.ModalCountClampsAndCancelAndLeaveClearThePreview`

Added tests in `tests/Presentation/Panels/ScenePathEditCommandsTests.cpp`:

- `ScenePathEditCommandsTests.LoopCutBindingResolvesOnlyInEditModeAndHonoursUserOverrides`
- `ScenePathEditCommandsTests.LoopCutCommandRequestsAPreviewWithoutMutatingOrUndo`

## 4. Reverse direction

`ReversePath` swapped start/end decorations along with reversing geometry, keeping an end arrow at the same physical endpoint. Removed that swap. Both Object Mode and Edit Mode reuse this operation. Existing gradient-stop mirroring and dash-phase mirroring remain in place.

Files: `src/Renderer/Path/PathTopology.cpp`, `tests/Renderer/Path/PathTopologyTests.cpp`, `tests/Presentation/Panels/ScenePathCutoverTests.cpp`.

Updated tests:

- `PathTopologyTests.ReverseKeepsEndpointRolesAndMirrorsGradientAndDashPhase` (renamed from the old decoration-swapping test; also checks sampled physical colours).
- `ScenePathCutoverCommandTests.ReverseSelectedPathsIsOneUndoAndLeavesUnselectedPath`

## Decisions beyond the task specification

- Cubic insertion uses equal original parameter intervals; lines and arcs therefore also have equal geometric intervals. Preview and commit use the same parameters.
- The hovered segment stays locked until commit/cancel. Escape cancels the preview and leaves Edit Mode active.
- Bevels taper to zero at the interface with an excluded part, preserving that part's sharp geometry.
- The Ctrl+R default is installed through the existing resolver after user keymaps have loaded, without editing `install/users/**`.
- Singular path scales reject insertion with a structured validation error.
- Existing atom bindings and buffer values are retained; buffer offsets may change to keep displayed endpoints fixed.

## Complete file list for this task

Production files (29):

- `src/IO/SceneObjectsIO.hpp`
- `src/IO/SceneObjectsYaml.cpp`
- `src/Presentation/EditorLayerMenus.cpp`
- `src/Presentation/Panels/ScenePathEditCommands.cpp`
- `src/Presentation/Panels/ScenePathEditCommands.hpp`
- `src/Presentation/Panels/ScenePathEditorWidget.cpp`
- `src/Presentation/Panels/ScenePathEditorWidget.hpp`
- `src/Presentation/Panels/ViewportInteraction.cpp`
- `src/Presentation/Panels/ViewportPathInsert.cpp` (new)
- `src/Presentation/Panels/ViewportPathInsert.hpp` (new)
- `src/Renderer/Path/PathCommands.hpp`
- `src/Renderer/Path/PathInsertCommands.cpp` (new)
- `src/Renderer/Path/PathEditSession.cpp`
- `src/Renderer/Path/PathEditSession.hpp`
- `src/Renderer/Path/PathPicking.cpp`
- `src/Renderer/Path/PathPicking.hpp`
- `src/Renderer/Path/PathSolidBevelGeometry.cpp`
- `src/Renderer/Path/PathSolidBevelGeometry.hpp`
- `src/Renderer/Path/PathSolidBevelTopology.cpp`
- `src/Renderer/Path/PathSolidBevelTopology.hpp`
- `src/Renderer/Path/PathSolidBeveler.cpp`
- `src/Renderer/Path/PathSolidMesher.cpp`
- `src/Renderer/Path/PathSolidMesher.hpp`
- `src/Renderer/Path/PathStrokeMesher.cpp`
- `src/Renderer/Path/PathStyle.hpp`
- `src/Renderer/Path/PathTopology.cpp`
- `src/Renderer/Path/PathTopology.hpp`
- `src/Renderer/RendererWindowState.hpp` (only this task's mesh-overlay default change)
- `src/Renderer/Scene/ScenePathPersistence.cpp`

Test files (8):

- `tests/Presentation/Panels/ScenePathCutoverTests.cpp`
- `tests/Presentation/Panels/ScenePathEditCommandsTests.cpp`
- `tests/Presentation/Panels/ScenePathEditorWidgetTests.cpp`
- `tests/Renderer/Gl/GlPathRenderTests.cpp`
- `tests/Renderer/Path/PathDecorationBevelTests.cpp`
- `tests/Renderer/Path/PathLoopCutTests.cpp` (new)
- `tests/Renderer/Path/PathTopologyTests.cpp`
- `tests/Renderer/Scene/ScenePathPersistenceTests.cpp`

Documentation: `docs/work/project/tasks/66-path-feedback-report.md` (this file, new).

Other working-tree changes belong to the concurrent work and are excluded from this report. This task did not edit the prohibited areas.

## Verification and remaining risks

- Checked added includes and signatures against the real project headers. Checked aggregate-initializer usage for the extended style/picking types.
- `git diff --check` passes. The expanded beveler and style widget remain below approximately 500 lines (458 and 480).
- Build and tests deliberately remain for the caller. Regenerate projects for the four new `.cpp`/`.hpp` files listed above.
- The bevel closure/shape matrix and OpenGL pixel regression need caller execution; the Ctrl+R mouse/keyboard interaction also needs a manual viewport check.
- `graphify update .` was attempted; the refresh failed with Windows access denied (`WinError 5`). The existing graph has not been successfully refreshed in this sandbox.

## Follow-up: caller's bevel closure failures

The caller's initial build succeeded, with 1166 passing tests, one skip, and failures in the two new bevel matrix/parts tests. The tests exposed construction errors; their closure, opposing-edge winding, signed-volume, and sharp-bounds assertions remain intact.

Causes found by tracing the construction and comparing it with the pre-task `HEAD` sources:

- **Winding:** source corner edges were ordered through an undirected adjacency graph. The emitter then guessed each strip/triangle's orientation from summed face normals. That heuristic can flip only part of a folded reflex patch, leaving neighbouring triangles traversing a shared edge in the same direction. Both mechanisms predate this task. The new winding check exposes a condition the old reference-count and normal-vs-triangle checks did not verify.
- **Four incident triangles:** my first fix made straight contour-midpoint profiles coincide but left the generic corner fan in place, adding duplicate triangles there. The midpoint duplication is a regression from that first fix. The original reflex-fan-before-pinch ordering was correct; changing it in this follow-up caused the shape-zero regression described below.
- **One incident triangle:** selective bevels taper to zero at the excluded part. Their end quads consequently contain a repeated corner; the polygon triangulator rejected those quads instead of emitting their nondegenerate triangle. The new tapering exposed this unhandled degenerate input.

The correction orders each corner through directed, outward-wound source faces and orients edge strips against those same face edges. Polygon emission now preserves that boundary winding rather than flipping individual patches toward a guessed normal. Pinched-span cancellation handles non-reflex trihedral corners and generic midpoint boundaries. Reflex corners retain task 61's fan-first handling. Repeated adjacent/closing positions are removed before triangulation, preserving every nonzero boundary edge while allowing tapered quads to emit a triangle.

The pre-task source comparison establishes that the winding heuristic already existed. The later caller results established that the reflex/pinch ordering was valid and needed to be restored. No baseline binary or test suite was run in this sandbox; there is no measured pre-task failure count to report.

Files changed in this follow-up:

- `src/Renderer/Path/PathSolidBeveler.cpp`
- `src/Renderer/Path/PathSolidBevelGeometry.cpp`
- `src/Renderer/Path/PathSolidBevelGeometry.hpp`
- `src/Renderer/Path/PathSolidBevelTopology.cpp`
- `src/Renderer/Path/PathSolidBevelTopology.hpp`
- `tests/Renderer/Path/PathDecorationBevelTests.cpp`
- `docs/work/project/tasks/66-path-feedback-report.md`

Added focused regressions, before the corresponding corrections:

- `PathDecorationBevelTests.CornerEdgeOrderFollowsTheOutwardFaceWinding`
- `PathDecorationBevelTests.SingleSegmentArrowKeepsOppositeSharedEdgeWinding`
- `PathDecorationBevelTests.StraightContourMidpointDoesNotAcquireADuplicateCornerPatch`
- `PathDecorationBevelTests.TaperedStripEmitsItsNondegenerateTriangle`

No new `.cpp`/`.hpp` files in this follow-up. No build, test execution, or commit. Headers, emitter callers, and whitespace were checked; caller execution of the original matrix and full suite remains required.

## Follow-up: restore shape-zero reflex closure

The caller built the correction successfully and reported 1170 passing tests. All shape-0.5/1 cases and every one-segment case passed. Shape zero with 4/16 segments still failed, and `PathStrokeMesherTests.BevelledFlatDecoratedStrokeIsOneClosedSurface` regressed at 4 segments/shape zero.

**Cause:** I incorrectly moved the shape-zero pinch splitter ahead of the reflex fan. The reflex shoulder boundary folds in projection and cannot be treated as a simple planar pinch loop. Its polygon emission omitted the required corner faces, leaving the sampled strip boundaries with only one incident triangle. Task 61's reflex-first ordering was intentional and correct; the previous report's claim that this ordering was defective has been corrected above.

Restored the source-anchored reflex fan before shape-zero pinch splitting. Non-reflex pinch splitting and generic midpoint cancellation remain in place, along with directed winding, compacted tapered quads, and the prior bounds fixes.

Added `PathDecorationBevelTests.ShapeZeroReflexShouldersKeepTheirFanAtEvenSegmentCounts` before changing the implementation. It checks Arrow shape zero at 4/16 segments for finite geometry, closure, opposing winding, outward triangle normals, and the sharp silhouette. The existing matrix and task-61 regression assertions remain unchanged.

Files changed in this follow-up:

- `src/Renderer/Path/PathSolidBeveler.cpp`
- `tests/Renderer/Path/PathDecorationBevelTests.cpp`
- `docs/work/project/tasks/66-path-feedback-report.md`

No new `.cpp`/`.hpp`, builds, test execution, or commits. The caller must rerun the two failing tests and full suite.
