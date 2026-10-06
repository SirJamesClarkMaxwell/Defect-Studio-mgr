# Task 67: bind a path node to a vacancy marker

Branch `task/65-feedback-round`. Another agent edits non-path files in the same working tree at the same time:
touch only what this task needs and list every file you change.

## Problem (user, hBN with V_B and V_N)

"Vacancy bonds" (`AddVacancyBonds` in `src/Presentation/Panels/ViewportVacancyAdd.cpp`) are ScenePath lines
whose atom end is bound (`PathBinding::CopyPosition`, buffer 0) but whose vacancy end is a FREE node placed
on the marker's edge. Moving a vacancy (G / gizmo / typed position) leaves the lines behind, and a line
between two vacancies follows neither. A vacancy is not a scene object (it is domain data,
`structure.vacancies`, indexed), so `ObjectOrigin` cannot express it.

## What to do

1. New binding kind `PathBinding::CopyVacancy { std::size_t vacancyIndex; glm::vec3 offset; float buffer; }`
   in `src/Renderer/Path/PathTypes.hpp` - same meaning as `CopyPosition`, but the position is
   `structure.vacancies[vacancyIndex].cartesianPosition` and `buffer` is in units of the vacancy marker radius
   (`RendererVacancyData::radius`): buffer 1.0 ends the line exactly on the marker's edge, toward the sole
   neighbour, with the same non-inversion clamp as the atom buffer. A stale index falls back to the authored
   position and reports BrokenBinding like the other kinds.
2. `BindingContext` (`src/Renderer/Path/PathBindingResolver.hpp`) gets `vacancyPosition` and `vacancyRadius`
   callbacks; fill them wherever `MakePathBindingContext` builds the context (SceneSystem) and anywhere else a
   context is built (render, export, pick, overlay, region select, pivots - grep for `atomRadius =`).
   The cache key that tracks binding sources (`BindingSourceRevision`, task 50a) must change when a vacancy
   moves, so lines refresh when a vacancy is moved.
3. Persistence: binding kind string "CopyVacancy" with the vacancy index (and offset, buffer) in the
   scene-objects YAML (`PersistedPathBinding`, `ScenePathPersistence`). Old files load unchanged.
4. Edit-mode binding UI (`ScenePathBindingOperations`, the N-panel binding section): show "Vacancy N"
   for this kind and allow "Detach (keep position)"; no new bind button is required.
5. `AddVacancyBonds` (`src/Presentation/Panels/ViewportVacancyAdd.cpp`): the vacancy end(s) become
   `CopyVacancy` with buffer 1.0 (atom-vacancy lines: start CopyPosition atom buffer 0, end CopyVacancy
   buffer 1; vacancy-vacancy lines: both ends CopyVacancy buffer 1). Remove the manual `VacancyEnd`
   shortening there (the binding does it). The same for `AddScenePathThroughSelectedAtoms` in
   `src/Presentation/Panels/ScenePathOperations.cpp`: a selected vacancy end binds `CopyVacancy` with the
   current path atom buffer.
6. Tests: resolver (follows the vacancy, buffer 1 ends on the edge, stale index falls back), YAML round
   trip, `ViewportVacancyAddTests` updated to assert the bindings (you may change those expectations), and a
   test that moving `structure.vacancies[i].cartesianPosition` changes the resolved line.

## Files that must NOT be touched

`Vendor/**`, `install/users/**`, `src/Domain/**`, `src/Presentation/Panels/ViewportDefectFrame.*`,
`src/Presentation/Panels/ViewportLabelInteraction.cpp`, `src/Presentation/Panels/ViewportInteraction.cpp`,
`src/Presentation/Panels/SceneOutliner*`, `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` label code,
`src/Renderer/Scene/SceneTransform*` (the other agent's area). If you need a change there, say so.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build; the caller builds
and sends errors. Report: files changed, tests added, decisions.
