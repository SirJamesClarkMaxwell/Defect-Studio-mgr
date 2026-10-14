# Task 112e: snap targets and picked axis

Implemented target snapping for modal Translate (including gizmo handle drags) and two-atom axis picking for Translate/Rotate. The existing transform preview, commit commands and duplicate undo grouping are retained.

## Files changed

- `src/Renderer/Scene/ModalSnapTargets.hpp` (new): target mode/kinds, candidate identity, the three specified pure APIs, target readout and picked-axis state.
- `src/Renderer/Scene/ModalSnapTargets.cpp` (new): atom/bond/cell candidates, screen-space picking with camera-depth tie breaking, constrained target translation, orthonormal picked-basis completion, axis-pick transitions and Polish target/residual readout.
- `src/Renderer/Scene/ModalTransform.hpp`: appended Picked orientation, optional picked basis, picked-axis label and basis-completion declaration. Existing public declarations remain available.
- `src/Renderer/Scene/ModalTransform.cpp`: resolves/cycles Picked, returns normal X/Y/Z constraints on the next axis key, and shows `oś A→B` with atom names and Angstrom units for picked numeric translation.
- `src/Renderer/RendererWindowState.hpp`: one local member block for per-window/session mode, cached candidates, current target, axis picking and the frozen preview delta; one include.
- `src/Presentation/Panels/ViewportModalTransform.cpp`: caches candidates once at start, consumes Tab/V and picking clicks, freezes movement while picking, overrides mouse translation for a target within 12 pixels, and draws the ring/dashed line plus readout/hints.
- `src/Presentation/Panels/ViewportTransformToolbar.cpp`: small Siatka/Cele toggle beside the existing magnet controls, with tooltip.
- `tests/Renderer/ModalSnapTargetsTests.cpp` (new): nine tests.
- `tests/Renderer/ModalTransformTests.cpp`: four added tests; original test text/expectations preserved.
- `docs/work/project/reports/task-112e-snap-targets.md`: this report.

## Behaviour and persistence

`snapTarget` is **per-window, per-session**, alongside the existing non-persisted transform orientation/pivot. The existing persisted `TransformTranslateSnap` values belong to the separate global viewport settings and are unchanged. Tab updates a copy used only by the current operation. Ctrl activates snapping and Ctrl+Shift retains fine increments when the normal increment path is used. No candidate uses the existing normal behaviour. Numeric input remains the explicit override, including fractions and invalid-input confirmation blocking.

Candidates exclude moved atoms and incident bonds, hidden atom/bond geometry, and globally hidden atom/bond groups. Cell points require the cell box to be enabled and a finite, non-singular meaningful cell (periodic structure or existing cell edges). The lattice generates the eight corners, twelve edge midpoints and six face centres, including skew cells.

V uses the existing viewport atom ray picker. The first valid click records A; the second distinct position establishes an Axis constraint in the Picked frame. Coincident positions keep picking B. Escape/right click during picking cancels only picking; both picking clicks are consumed without committing. Rotation retains the current pivot. Numeric picked translation is in Angstroms even if the normal orientation is Lattice. X/Y/Z return to that normal orientation. Existing modal keyboard capture prevents the V saved-view binding from firing; path-edit commands already ignore active modal transforms.

**Periodic-image limitation:** atom targets are only the actual entries in the window atom list; no synthetic periodic atom images are generated. `RendererAtomData` has no image metadata, so these targets retain `image = (0,0,0)`. Bond midpoints use the window bond's existing `secondAtomPeriodicOffset`, matching its displayed segment without synthesizing atom entries.

## Tests added

`ModalSnapTargetsTests`:

1. `CollectsAtomsBondAndAllTwentySixCellPoints`: exact kind counts and midpoint/endpoint identity.
2. `ExcludesMovedAtomsAndIncidentBonds`: excludes the moved atom and bond while retaining all cell targets.
3. `UsesDisplayedPeriodicBondAndNeverSynthesizesAtoms`: displayed periodic bond midpoint and unchanged atom target count/image fields.
4. `RejectsHiddenInvalidGeometryAndMeaninglessCell`: hidden atoms, invalid bond indices, singular/nonperiodic cells and non-finite positions.
5. `PicksNearestInPixelsAndBreaksTiesTowardCamera`: orthographic nearest/radius tests, camera-depth tie, viewport origin.
6. `RejectsClippedNonFiniteAndInvalidPickInputs`: near/far clipping, non-finite position, empty targets, negative radius and zero viewport size.
7. `TranslatesExactlyOrProjectsThroughTheAnchor`: exact None, X axis, Z plane and nonzero anchor.
8. `HeadersNameTheTargetAndShowConstraintResidual`: atom/bond/corner labels and 0.37 Angstrom residual.
9. `AxisPickingRequiresTwoDistinctPositionsAndRetainsThePivot`: A/B transitions, coincident rejection, picked direction, labels and pivot preservation.

`ModalTransformTests`:

1. `PickedBasisIsRightHandedOrthonormalAndRejectsDegeneratePoints`: normalized direction, orthonormality, determinant +1, polar directions, coincident/non-finite rejection.
2. `PickedConstraintCyclesAndNormalKeysReturnToTheirOrientation`: resolution/fallback, Picked/Global/None cycle and normal X recovery.
3. `PickedNumericFractionsUseAngstromsAndHeaderNamesTheAtoms`: 1/8 Angstrom with a non-unit lattice, A→B labels and unit readout.
4. `PickedRotationUsesCurrentPivot`: numeric rotation about the picked axis with a nonzero preserved pivot.

## Validation

- Read AGENTS.md, CLAUDE.md, the task, its named plan, and the 112a/112b prerequisite reports before implementation; queried Graphify before code navigation.
- Native MinGW GCC C++23 **syntax-only** checks passed for `ModalSnapTargets.cpp`, `ModalTransform.cpp`, both changed viewport translation units, and both test translation units, using the generated test project's include directories and platform definitions. No executable was built or run.
- Scoped `git diff --check` passed. Compared the original modal test file against HEAD after removing the added test block: original text is exact, excluding line endings. Edited existing files retain CRLF, blank lines and brace style.
- Ran the requested project-generation command with `DS_TOOLSET=msc-v143`. Premake completed successfully and the generated application/test projects contain the new source/header/test entries. The batch's git submodule step emitted the known Git Bash `CreateFileMapping` sandbox error; generation still completed with exit code 0.
- Context7 tools were unavailable. New ImGui calls were checked against the [official ImGui header](https://github.com/ocornut/imgui/blob/master/imgui.h), and viewport syntax checks passed against this checkout.

## Anything not done

- No MSBuild, test execution, interactive viewport check or commit, as requested. Test pass status awaits the user's build/run.
- Graph update deferred: `graphify update .` writes `graphify-out/`, outside the explicit allowed-file list. Graph queries were used.
- The pre-existing oversized `ModalTransform.cpp` and `ModalTransformTests.cpp` were not split or reformatted. Basis construction and target/picking logic are in the new renderer file; the viewport glue is approximately 500 lines (506).
- No files owned by task 109c/110, Phonon/plot/CellPanel files, Vendor/install files or other documentation were edited by this task. Build project files changed only through the explicitly requested generator. Other working-tree edits were left intact.
