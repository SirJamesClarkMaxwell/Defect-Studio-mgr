# Task 112f: pivot and distance from a picked atom

Implemented pivot picking (`P` for Rotate/Scale) and distance-reference picking (`D` for Translate), building on the committed 112e picking and snap-target flow. Existing public declarations and existing test text are preserved.

## Files changed

- `src/Renderer/Scene/ModalTransformReferences.cpp` (new): pure distance solver, shared confirmation validation, pivot/distance header parts and generalized atom-pick transitions. Renderer code has no ImGui, Presentation or App dependency.
- `src/Renderer/Scene/ModalTransform.hpp`: appended session reference fields and additive API declarations; documented target-distance numeric evaluation.
- `src/Renderer/Scene/ModalTransform.cpp`: small hooks for target-distance evaluation, Angstrom numeric readout and reference header parts.
- `src/Renderer/Scene/ModalSnapTargets.hpp`: appended picking purpose (`AxisA`, `AxisB`, `Pivot`, `DistanceReference`), mouse-aware Pick overload and prompt API. The existing three-member aggregate initialization and Pick signature remain usable.
- `src/Renderer/Scene/ModalSnapTargets.cpp`: moved the existing Pick definition into the focused reference implementation, retaining the existing target math and formatting.
- `src/Renderer/RendererWindowState.hpp`: one local two-line block for the optional folded preview snapshot.
- `src/Presentation/Panels/ViewportModalPicking.{hpp,cpp}` (new): shared V/P/D picking glue, moved-atom exclusion, click consumption, picking-only cancellation and pivot snapshot folding.
- `src/Presentation/Panels/ViewportModalTransform.cpp`: calls the extracted picking glue, uses the folded snapshot and picked pivot, adds operation-specific hints, and checks shared confirmation validation. Clears folded state at begin/reset and when a pending duplicate switches operation.
- `tests/Renderer/ModalTransformTests.cpp`: 12 added tests; all original tests retain their exact text and expectations.
- `docs/work/project/reports/task-112f-pivot-distance.md`: this report.

The user explicitly approved focused new implementation files and the small Presentation header. New implementation files have 165 and 92 lines. `ViewportModalTransform.cpp` shrank from 506 to 499 lines. The pre-existing 625-line `ModalTransform.cpp` has only four net additional hook lines (629 total); no unrelated code was split or reformatted.

## Pivot fold and undo choice

**Fold into a separate preview start snapshot.** A valid pivot pick captures the currently visible selection into `modalTransformPreviewSelection`, changes the session pivot to the picked atom, resets both mouse reference positions to the click, clears numeric input, and resets angle accumulation to zero. Subsequent deltas apply to that folded snapshot with a single pivot, overriding IndividualOrigins for the gesture. Scale uses the restarted mouse reference with the existing scale math and zero-radius guard.

The original `modalTransformSelection` and scene-object undo snapshot remain unchanged. Escape outside picking restores the original selection; final confirmation still uses the existing command/group commit path once. Picking does not execute commands or open/close undo groups. Pending duplicate operation switching still restores the original copies and clears folded state, while retaining a picked pivot label.

## Picking and distance behaviour

One click-to-atom path serves V/P/D. It reuses `PickAtomAlongRay` with a local atom-geometry copy whose moved atoms are invisible, allowing picks behind moved atoms without modifying the displayed window. Existing visibility, sphere padding and nearest-hit rules are reused. Axis picking still takes two distinct positions. P in Translate and D in Rotate/Scale do nothing and are absent from those operations' hints. Escape/right click during picking consumes the frame and cancels only picking. Handle-started gestures become ordinary modal gestures when reference picking starts, so releasing the handle or clicking a reference does not automatically commit them.

Without numeric text, setting a distance reference leaves the normal mouse/target-snap translation path unchanged and adds a live distance readout. Numeric distance targets ignore snapping and lattice-unit scaling. None uses the reference-to-anchor radial ray; Axis chooses the sphere intersection with smallest absolute travel and the positive basis direction on ties; Plane uses the actual span of the two included basis columns, including skew cells. Negative, non-finite, unreachable and degenerate inputs return nullopt. Invalid/unreachable target input evaluates to zero translation, displays `nieosiągalne`, and blocks confirmation. The header and confirmation gate share the same validation helper.

## Tests added

Added to `ModalTransformTests.cpp`:

1. `DistanceRadialTranslationReachesTargetAndRejectsCoincidentAnchor`: required radial example, zero target and coincident rejection.
2. `DistanceAxisChoosesNearestRootAndPositiveDirectionOnTies`: positive tie, nearest root on both sides, miss and tangent.
3. `DistancePlaneFindsNearestCirclePointAndRejectsItsCentre`: required circle example, centre, unreachable plane and tangent.
4. `DistancePlaneUsesTheSpanOfSkewLatticeColumns`: translated reference and skew excluded column.
5. `DistanceRejectsInvalidInputsAndDegenerateConstraints`: negative/non-finite values, invalid axis and degenerate bases.
6. `NumericDistanceIsATargetInAngstromsIncludingLatticeUnits`: target 2.5 Angstrom in Global and non-unit Lattice orientations, constraint preservation and header units/label.
7. `DistanceReferenceLeavesMouseTranslationUnchangedAndShowsLiveDistance`: unchanged mouse translation and live readout.
8. `UnreachableOrInvalidDistanceReturnsZeroAndBlocksConfirmation`: unreachable, negative and unparsable targets, recovery with a valid fraction, and ordinary negative-displacement compatibility.
9. `PivotPickRestartsRotateAndScaleReferencesAndNamesTheAtom`: pivot position, start/last mouse, angle/numeric reset, identity at restart and header label for both operations.
10. `ReferencePickPurposesRespectOperationsAndAxisPickingStillHasTwoClicks`: unsupported operation guards, distance reference and A/B purpose transitions.
11. `ModalAtomPickerIgnoresMovedAtomsWithoutChangingTheirVisibility`: picks an unmoved atom behind the moved atom, preserves visibility, and rejects hidden/globally hidden atoms.
12. `PivotFoldPreservesPreviewOriginalSnapshotAndSinglePivotApplication`: Rotate/Scale preview fold, moved-atom rejection, unchanged original snapshot, single-pivot application despite IndividualOrigins preference, and original-position restoration.

## Validation

- Read AGENTS.md, CLAUDE.md, the task, named plan and 112e/112a/112b reports; queried Graphify before source navigation.
- Native MinGW GCC C++23 syntax-only checks passed for `ModalTransform.cpp`, `ModalTransformReferences.cpp`, `ModalSnapTargets.cpp`, both changed viewport translation units, `ModalTransformTests.cpp` and unchanged `ModalSnapTargetsTests.cpp`, using generated test-project include directories and platform definitions. No executable was built or run. The final reference-math edit was checked again.
- Original `ModalTransformTests.cpp` text matches HEAD exactly after removing only the added includes/tests, excluding line endings. Original 112e test file is untouched.
- Scoped `git diff --check` passed. Edited and new C++ files use CRLF; existing blank lines, multiline guards and brace style are preserved.
- Ran the requested `DS_TOOLSET=msc-v143` project-generation command successfully (exit 0). Application and test projects contain both new `.cpp` files and the new header. The batch's submodule step emitted the known Git Bash CreateFileMapping sandbox error; Premake completed successfully.

## Anything not done

- No build, test execution, MSBuild, interactive viewport validation or commit, as requested. Runtime/test pass status awaits the user's build and test run.
- Graphify update deferred because graphify-out/ remains outside the explicitly allowed task file scope; graph queries were used.
- Existing zero-start-radius mouse scaling retains its identity fallback; typed scale factors remain available. No existing scale expectation was changed.
- Task 109d/111 files, Phonon/plot/figure files, IO, CellPanel/CellEditing, Vendor and install files were not edited by this task. Generated project files were changed only by the requested generator. Concurrent changes were left intact.
