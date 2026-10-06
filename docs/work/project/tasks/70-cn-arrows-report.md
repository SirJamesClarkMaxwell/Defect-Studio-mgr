# Task 70 report

Implemented shallow C_n arrows and angle-sorted cycles for three or more selected atoms.

## Files changed

Authored changes (only these seven files):

- `src/Presentation/Panels/ScenePathCurvedArrow.hpp` — new batch-result declaration.
- `src/Presentation/Panels/ScenePathCurvedArrow.cpp` — arrow geometry, axis resolution, ordering, validation and batch insertion.
- `src/Presentation/Panels/ScenePathOperations.hpp` — includes the new declaration.
- `src/Presentation/Panels/ScenePathOperations.cpp` — removed the old curved-arrow implementation and its unused binding-resolver include; now 408 lines.
- `src/Presentation/Panels/ViewportAddMenu.cpp` — enables the item for at least two atoms, retains two atom/vacancy ends, and explains cycles and axes in a Polish tooltip with diacritics.
- `tests/Presentation/Panels/SceneCurvedArrowTests.cpp` — updated geometry tests and added cycle/undo coverage.
- `docs/work/project/tasks/70-cn-arrows-report.md` — this report.

Project generation refreshed these ignored build artifacts:

- `build/generated/vs2022/DefectStudio.vcxproj`
- `build/generated/vs2022/DefectStudio.vcxproj.filters`
- `build/generated/vs2022/DefectStudioTests.vcxproj`
- `build/generated/vs2022/DefectStudioTests.vcxproj.filters`

Graph navigation and the AST refresh also updated these ignored artifacts:

- `graphify-out/.graphify_learning.json`
- `graphify-out/graph.json`
- `graphify-out/manifest.json`
- `graphify-out/cache/stat-index.json`
- `graphify-out/reflections/LESSONS.md`
- `graphify-out/cache/ast/v0.9.17/0a090f49a5dae4798a2a9f6560a9e61624854d2cf5c5758f92edd5a0390c64f1.json`
- `graphify-out/cache/ast/v0.9.17/0cdc232a7475e94cc1639b17d629298efadf2ecf8603eaee468d336f84767c69.json`
- `graphify-out/cache/ast/v0.9.17/16cda9bdd6230663b2520b1b30837377c14f078a11584a5daaec26205aafdb5d.json`
- `graphify-out/cache/ast/v0.9.17/6855768ae6396b2d6801c44cd49aa5dcf18f5556cb2a62c6b5053f126fbe3d0c.json`
- `graphify-out/cache/ast/v0.9.17/c5695ede029978fe320db8a0a5692aebd6272fda7627481f52cf13ca7f849c93.json`
- `graphify-out/cache/ast/v0.9.17/eb4c5d3f50494daa11d17fbf27321a0ffe483548731a78102a651b1897941862.json`

The caller's concurrent vacancy-hiding changes and existing user configuration changes were left alone. No commit or build was performed.

## Geometry and style

- Named tuning constant: `kCurvedArrowFlatness = 0.5f`; authored arc sweep is half the endpoint rotation angle.
- A 120-degree C_3 step becomes a 60-degree circular arc. Its sagitta is `chord * tan(15 degrees) / 2`, or **0.133974596 of the chord**. At atom radius 2, the unbuffered midpoint is radius **1.464101615**, versus the old orbit's radius 2 (chord midpoint radius 1).
- Endpoint tangents deviate 30 degrees from the chord for C_3, versus 60 degrees previously.
- Shaft width: **0.03** world units (default Arrow: 0.05).
- End decoration: filled **Arrow**, `lengthScale = 3.0`, `widthScale = 1.0` (default widthScale: 1.25). With the thinner shaft, the head is smaller overall and its width/length ratio is slimmer.
- Existing round profile, colour and depth testing remain in use. No renderer feature or path-style schema was added.

## Decisions

- Reuse `CircularArcSegmentData` and the existing `DeriveArc`: it derives a circle from the resolved chord and projects the normal perpendicular to that chord. Unequal endpoint heights/radii therefore also work without bespoke Bezier controls.
- Both ends retain `CopyPosition` / `CopyVacancy` bindings and the usual session atom buffer. The arc is derived from the shortened, buffered chord, so its sagitta scales with the visible gap and its ends continue to follow their sources.
- Exactly two ends retain selection order A -> B and the existing signed-angle and axis rules, including neighbour-based fallback and vacancy endpoints.
- For three or more valid selected atoms, selected vacancies are never endpoints. Axis priority: defect frame origin/z; otherwise the existing least-squares `FitScenePlane` normal through a single selected vacancy, or through the fit's centroid.
- A fitted plane has no intrinsic sign: its largest-magnitude normal component is made positive. A defect frame's signed z is respected. Sorting is camera-independent and connecting adjacent sorted atoms includes the wrap-around pair in the positive sense.
- Each cycle pair uses its positive angular gap, including gaps larger than 180 degrees; flatness is a sweep multiplier, so the 0.134 sagitta ratio specifically describes a 120-degree step rather than every possible gap.
- All paths are prepared and validated before insertion. Insertions use the existing silent edit context; an insertion failure restores the captured scene snapshot. A successful call pushes one shared undo snapshot, selects every new path, and returns their IDs.
- Nonfinite positions/axes, endpoints on the axis, and pairs at the same angular position are rejected without partial geometry or an undo entry.

## Validation

Eleven C++ tests are provided across `SceneCurvedArrowTests` and `SceneCurvedArrowUndoTests`:

- 120-degree pair: expected outward sagitta, significantly flatter than the old orbit, lighter style, bindings and following a moved endpoint.
- Two atoms with a selected vacancy axis.
- Buffered ends with different atom radii: surface clearance and sagitta relative to the resolved chord.
- Atom/vacancy endpoints at unequal axis radii and heights.
- Reversed two-atom selection retains negative signed rotation and A -> B order.
- Shuffled C_3 selection with centroid and off-centre vacancy axes; vacancy placement affects sweeps.
- Best-fit normal for a vertical yz-plane cycle.
- Four-atom cycle around a negative defect z axis.
- Missing selection and endpoints on the axis.
- Shuffled C_3 cycle with frame priority: three consecutive pairs, selection of all three, exactly one undo record, removal of the whole batch on undo, and restoration on redo.
- A later invalid pair and nonfinite input leave no partial cycle or undo record.

**C++ tests were not run:** the task explicitly reserves builds for the caller. After building, run:

```text
DefectStudioTests.exe --gtest_filter=SceneCurvedArrowTests.*:SceneCurvedArrowUndoTests.*
```

Checks performed here:

- Analytic Python check passed: C_3 sagitta/chord 0.133974596 and sagitta 0.464101615 at radius 2.
- Angular-order check passed for three shuffled triangle orders, with both centroid and off-centre vacancy axes.
- `git diff --check` passed for authored changes.
- Source files remain below 500 lines.
- Ran `scripts/Windows/GenerateProjects.bat` with `DS_TOOLSET=msc-v143`; exit code 0. Both generated projects include the new source/header. Its Git submodule helper emitted a Windows `CreateFileMapping` access error; Premake nevertheless generated the projects successfully. No build was attempted.
- `graphify update .` failed with Windows `Access denied`; a scoped `_rebuild_code` retry was stopped because it still scans the whole repository. Completed the graphify skill's AST/merge fallback instead: serial extraction of the six changed code files, `build_merge` preserving all unchanged-source node IDs, `to_json`, and a subset AST manifest update. Result: **108,787 nodes / 223,183 edges**. Prior community assignments were retained; `GRAPH_REPORT.md` and community clustering were not regenerated. A follow-up graph query resolves `AddCurvedArrowThroughSelectedAtoms` to the new implementation at `ScenePathCurvedArrow.cpp:72`. No LLM/API calls were used.

Visual comparison in the application remains for the caller after building.
