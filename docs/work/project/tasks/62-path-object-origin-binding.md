# Task 62: path S14 - ObjectOrigin binding survives save/load and is offered in the UI

Path system plan `docs/work/project/plans/2026-09-20-path-system-implementation.md`, S14, open item
"ObjectOrigin binding (needs a persistence second pass)".

## Goal

A path node can be bound to another scene object's origin (a free label, an orbital, a plane, an
arrow, another path), the binding is saved by the target's persistKey, and it is restored on load.
Today the runtime binding (`PathBinding::ObjectOrigin` in `src/Renderer/Path/PathTypes.hpp`) and its
evaluation exist, but `ScenePathPersistence` drops it on load with
`scene_objects.path_binding_unresolved`, so the UI refuses to offer it
(`path.binding_kind_unsupported`).

## What to do

1. Load: resolve ObjectOrigin in a second pass after every scene object of the structure has been
   applied (they all have their persistKey by then): persistKey -> current SceneObjectId. An unknown
   key keeps today's behaviour (warning, node stays free at its stored position). A key that names a
   path the node belongs to is rejected the same way (`PathEvaluator`'s
   `ObjectOriginTargetsPath` rule).
2. Save: already writes `objectPersistKey`; make sure the target's persistKey exists at save time
   (`EnsureScenePersistKeys`) - an object without one cannot be referenced.
3. UI (`src/Presentation/Panels/ScenePathBindingOperations.{hpp,cpp}` and the binding UI from task
   50b): offer "Bind to object origin" for the active node when exactly one other scene object is
   selected; drop the `path.binding_kind_unsupported` refusal for this kind; keep one undo entry per
   call (SetScenePathBinding).
4. Tests: a round trip (window -> PersistedSceneObject -> YAML -> window) where a path node bound to a
   free label's origin comes back bound to the reloaded label's NEW SceneObjectId; an unknown key
   warns and frees the node; the binding operation binds/unbinds with one undo entry.
5. Update the S14 status line in the plan file.

## Files to create or change

- `src/Renderer/Scene/ScenePathPersistence.{hpp,cpp}`, `src/Renderer/Scene/SceneObjectPersistence.cpp`
  (only to call the second pass after everything is applied)
- `src/Presentation/Panels/ScenePathBindingOperations.{hpp,cpp}` and the panel that shows the binding
  buttons (find it from task 50b: `docs/work/project/tasks/50b-path-binding-ui.md`)
- tests under `tests/Renderer/Scene/` and `tests/Presentation/Panels/`
- `docs/work/project/plans/2026-09-20-path-system-implementation.md` (S14 status line only)

## Files that must NOT be touched

- `src/Renderer/Path/PathTypes.hpp`, `src/Renderer/Path/PathEvaluator.*` (runtime binding is done)
- `src/IO/SceneObjectsIO.*` schema (the key is already there) unless a test proves a parse gap
- `src/Domain/**`, `Vendor/**`, `install/users/**`
- the legacy SceneArrow code (S16 removes it; do not start that here)

## Acceptance criteria

1. New tests pass; the full suite has no new failures (known: 5 `PathStrokeMesherTests` bevel cases).
2. In the app: bind a path end to a free label's origin, move the label, the path end follows; save,
   reopen, it still follows.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints): layers, no exceptions in render
paths, `.cpp` < ~500 lines, regenerate with DS_TOOLSET=msc-v143, MSBuild path, one build at a time,
private Vendor repos untouched, do not commit.
