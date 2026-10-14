# Task 112f: pivot on a picked atom (`P`) and a move to a set distance from an atom (`D`)

Plan: `docs/work/project/plans/2026-10-13-structure-building-gizmo.md` (slice 2, second part; rows "Odniesienia" and
"Odległość od atomu"). Builds on 112e (atom picking during a modal operation for `V`, snap targets). User
2026-10-13: "szybki sposób transformowania elementów np. względem innych atomów".

## Goal

Two keys inside a running modal operation, both picking an atom with one left click (same picking machinery as
`V` from 112e):

- `P` in `R`/`S`: the picked atom becomes the pivot for the rest of the operation (rotate S about Cu, scale a
  tetrahedron about its centre atom). Header `punkt obrotu: Cu12`.
- `D` in `G`: the picked atom C becomes the distance reference. The header shows the live distance
  `odległość od Cu12: 2.35 Å`; a typed number is the TARGET distance from C (not a displacement): the anchor moves
  along the current constraint path until |anchor - C| equals it (chalcopyrite: `G X D` click Cu, type `2.30`,
  Enter -> S slides along a until Cu-S = 2.30 Å).

## Design (binding)

1. **Picking**: generalise the 112e picking state to a purpose (`AxisA`, `AxisB`, `Pivot`, `DistanceReference`),
   one place that turns a click into an atom index; Esc while picking cancels the picking only. Picking ignores
   the moved atoms. `P` in `G` and `D` in `R`/`S` do nothing (not shown in the hints for that op).
2. **Pivot (`P`)**: the session pivot becomes the picked atom position; IndividualOrigins is overridden by the
   single pivot for the rest of the operation. The rotate angle accumulation restarts at the pick
   (`startMouse = lastMouse = mouse at pick`, `accumulatedAngleRadians = 0`) and the transform so far is kept by
   folding it into the start snapshot or by resetting the delta - choose the one that keeps a single undo entry
   and say which in the report. Scale restarts its reference distance the same way.
3. **Distance (`D`)**, pure function in `ModalTransform.{hpp,cpp}`:
   ```cpp
   // Translation that moves `anchor` along its constrained path to the point at `distance` from `reference`.
   // None: along the ray reference -> anchor (radial). Axis: the line anchor + t * axis, the root with the
   // smallest |t|. Plane: the circle in the plane through anchor, the point nearest to anchor.
   // nullopt when the path never reaches that distance (distance < 0, line misses the sphere, anchor == reference
   // under None, or anchor at the circle centre under Plane).
   [[nodiscard]] std::optional<glm::vec3> TranslationAtDistance(const glm::vec3 &anchor, const glm::vec3 &reference,
       float distance, const TransformConstraint &constraint, const TransformBases &bases);
   ```
   Session gets `std::optional<glm::vec3> distanceReference` (+ a label for the header). With a reference set
   and numeric text present, `EvaluateModalTransform` Translate uses `TranslationAtDistance(anchor, reference,
   value, ...)` (value in Å whatever `latticeUnits` says - the header says `Å`); nullopt -> zero translation and
   the header shows `nieosiągalne` and Enter is blocked (as for an unparsable value). Without numeric text the
   mouse path is unchanged and only the header gains the live distance.
4. **Header / hints**: `FormatModalTransformHeader` gains the pivot / distance parts; the hint line adds
   `P punkt obrotu (atom)` for R/S and `D odległość od atomu` for G.
5. Undo: still one command per operation.

## Files to create or change

- `src/Renderer/Scene/ModalTransform.{hpp,cpp}`, `src/Presentation/Panels/ViewportModalTransform.cpp`,
  `src/Renderer/RendererWindowState.hpp` (picking purpose), the 112e files if the picking state lives there
- tests: additions to `tests/Renderer/ModalTransformTests.cpp`

## Files that must NOT be touched

- Phonon files, plot files, `src/IO/*`, `CellPanel*`, `CellEditing*`
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-112f-pivot-distance.md`

## Acceptance criteria (GoogleTests)

1. `TranslationAtDistance`: None, anchor (2,0,0), reference origin, distance 3 -> (1,0,0); Axis x, anchor (0,1,0),
   reference origin, distance sqrt(2) -> (+1,0,0) or (-1,0,0) with the smaller |t| rule documented by the test
   (tie -> positive direction); Axis x, anchor (0,5,0), distance 1 -> nullopt; Plane z through (0,0,1), reference
   origin, anchor (2,0,1), distance sqrt(2) -> anchor ends at (1,0,1).
2. `EvaluateModalTransform` with a distance reference and typed `2.5` puts the anchor 2.5 Å from the reference
   along an Axis constraint, also in Lattice orientation with `latticeUnits` on.
3. Header contains `odległość od` with the label and `punkt obrotu` after a pivot pick; `nieosiągalne` for nullopt.
4. Existing tests unchanged and green. Report: files, tests, the pivot-fold choice.

## Constraints

- Renderer math stays ImGui-free; Presentation only translates input.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
