# Task 63: path S15 - cutover (every arrow/line is created, saved and loaded as a ScenePath)

Plan: `docs/work/project/plans/2026-09-20-path-system-implementation.md` S15, inventory and parity gate in
`docs/work/project/plans/path-legacy-inventory.md` (re-run its grep first - the footprint moved: tasks
59-62 added `src/Presentation/Panels/ViewportVacancyAdd.cpp` (vacancy bonds), defect-axes parenting
(`RendererWindowState::defectFrameChildren.arrows`) and align-to-axes, all on SceneArrow).

S15 changes ROUTING ONLY: after it no user action creates a legacy `SceneArrow`, nothing saves one, and a
loaded v1 arrow becomes a ScenePath. The legacy code stays compiled (S16 deletes it).

## Parity gaps to close first (decisions already taken)

1. Object Mode reverse for selected paths: `renderer.scene_path.reverse` (the shortcut legacy arrows used,
   Alt+R; keep the keymap entry, point it at paths), one undo entry for all selected paths.
2. "Adjust last operation" quick-edit popup: DROPPED. After any add the new path is selected, so Object
   Properties / the N panel show it.
3. Draw through selected atoms (Add > Rysuj (2 atomy) > Linia / Strzałka): a ScenePath Line segment whose
   two end nodes are bound `CopyPosition` to the two atoms with the current atom buffer (the legacy
   `GetSceneArrowAtomBuffer` value) - Linia without end decoration, Strzałka with an Arrow end decoration.
4. Free line / free arrow (Add > Linia swobodna / Strzałka swobodna, the toolbar add-arrow button): a free
   ScenePath of the same default length the legacy add used.
5. Handle markers in the structure-creation viewport: DROPPED for paths (paths have no Object Mode
   handles); remove the `DrawSceneArrowHandleMarkers` call there.
6. Vacancy bonds (`AddVacancyBonds`): a ScenePath Line, Round profile, bond thickness, two-stop gradient
   atom colour -> vacancy colour, start node bound `CopyPosition` to the atom with buffer 0, end node free
   at the vacancy. Update `tests/Presentation/Panels/ViewportVacancyAddTests.cpp` to assert the same intent
   on paths (count, binding, gradient colours, selection = the new paths) - you are authorised to change
   those expectations, nothing else's.
7. Defect-axes parenting: `defectFrameChildren.arrows` becomes `defectFrameChildren.paths`
   (SceneObjectIds of ScenePaths) and is captured like the other children
   (`CaptureSceneTransformSelection` / `CaptureSceneTransformPaths`); "Przypnij zaznaczone" pins selected
   paths. Align-to-axes already handles paths.
8. The "Path (dev)" Add submenu becomes the user-facing "Path" submenu (Line / Cubic / Arc presets, the
   decoration gallery stays under a "Dev" item).

## Routing

- Every creation entry point above and any other found by the grep -> ScenePath.
- Save: `ExtractPersistedSceneObjects` writes no `PersistedSceneArrow` (keep the v1 parse for loading).
- Load: v1 `PersistedSceneArrow` entries are migrated with the existing `MigrateArrowToPath` into
  ScenePaths; `window.sceneArrows` stays empty after a load. Report migration problems the way the
  migration already does.
- Menus/shortcuts that only make sense for legacy arrows (Arrow copy/paste geometry/style submenu in the
  context menu) are removed from reachability or pointed at the path equivalents where those exist
  (`ScenePathOperations`).

## Tests

- New/updated: reverse command (one undo, all selected paths reversed); draw-through-atoms creates a path
  with two CopyPosition bindings; free line/arrow creates a free path; vacancy bonds (item 6); loading a v1
  file with arrows yields paths and no sceneArrows; saving writes no arrows; parenting carries a pinned path.
- Existing legacy-arrow tests that test the legacy code itself keep passing (the code still exists).

## Files that must NOT be touched

- `Vendor/**`, `install/users/**`, `src/Domain/**`
- `src/Renderer/Path/PathStyle.hpp`, the path evaluator/mesher internals (`src/Renderer/Path/PathSolid*`,
  `PathStrokeMesher*`, `PathDecorationMesher*`) - S15 is routing, not geometry
- Do not delete legacy SceneArrow code (that is S16)

## Acceptance

1. Full suite green except nothing (the bevel failures were fixed in task 61; 1 skip is expected).
2. In the app: every Add entry creates a path; a v1 project with arrows opens with paths; save + reopen
   keeps them.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints).
