# Task 41 S6: ownership, kind, registry mirror, snapshot slice, caches

## Goal
A window can own paths. `PathStore` holds them by `SceneObjectId` with a geometry and a style revision
each; `PathSystem` pairs that copyable store with a `PathCaches` that must not be copied with it; the
scene registry mirrors one entity per path the way it already does for arrows; undo snapshots and the
export preview carry the store. Nothing is drawn and no path exists at runtime yet - the gate is "no
behaviour change", and the point of the stage is that S7 can upload geometry it did not recompute.

## Files to create
- `src/Renderer/Path/PathStore.cpp`
- `src/Renderer/Path/PathCaches.cpp`
- `src/Renderer/Path/PathSystem.cpp`
- `tests/Renderer/Path/PathStoreTests.cpp`
- `tests/Renderer/Path/PathCachesTests.cpp`
- `tests/Renderer/Path/PathSystemTests.cpp`

## Files you may change
- `src/Renderer/Scene/SceneSystem.cpp` - three additions, nothing else:
  1. `EnsurePathSystem(RendererWindowState &)` - creates `windowState.paths` with `CreateUnique<PathSystem>()`
     if it is null, returns `*windowState.paths`.
  2. `AppendScenePath(RendererWindowState &, ScenePath)` - allocates a fresh id from
     `windowState.sceneRegistry`, overwrites `path.id` with it, inserts into the store, returns the id.
     Exactly the `AppendSceneArrow` rule (`SceneSystem.cpp:307`), including discarding any incoming id.
  3. In `SyncLabelEntities`, a path block following the arrow/orbital blocks: destroy and clear
     `scene.PathEntities()` alongside the other four at the top, then, when `windowState.paths` is not
     null, walk the store in order and create one `SceneObjectKind::ScenePath` entity per path with
     `sourceIndex` = the position in the store, the path's `name` (or `"path " + index` when empty) as
     the display name, and the path's own id passed through so identity survives a resync. Give each a
     `TransformComponent` at the mean of the path's authored node positions (empty node list -> origin)
     and a `SelectionComponent{false}` - there is no `selectedScenePaths` vector yet and S6 must not add
     one. A path whose id was unset gets one allocated the same way the arrow block does, which means
     writing it back into the store through `MutateStyle` (it is not a geometry change).
- `src/Renderer/Commands/SceneObjectsSnapshotCommand.cpp` - `CaptureSceneObjectsSnapshot` also copies
  `window.paths ? window.paths->Store() : PathStore{}` into the new `paths` field;
  `RestoreSceneObjectsSnapshot` calls `SceneSystem::EnsurePathSystem(window).ReplaceStore(std::move(snapshot.paths))`
  before the existing `SyncLabelEntities` call. Nothing else in that file changes.
- `src/Renderer/RendererLayer.cpp` - `PopulateExportPreviewState` only: after the `scenePlanes` copy, if
  `source.paths` is not null, `SceneSystem::EnsurePathSystem(previewState).ReplaceStore(source.paths->Store())`.
  One statement.
- `tests/Renderer/Scene/SceneSystemTests.cpp` - add mirror-sync cases only; do not alter existing cases.
- `tests/Renderer/SceneObjectsSnapshotCommandTests.cpp` - add path round-trip cases only; do not alter
  existing cases.

## Files that must NOT be touched
- `src/Renderer/Path/PathStore.hpp`, `PathCaches.hpp`, `PathSystem.hpp` - the contract. If a signature is
  wrong, STOP and say which and why; do not edit it.
- `src/Renderer/RendererWindowState.hpp`, `src/Renderer/Scene/SceneSystem.hpp`,
  `src/Renderer/Scene/SceneObject.hpp`, `src/Renderer/Scene/SceneRegistry.{hpp,cpp}` - the `paths`
  member, the snapshot field, the two declarations, `SceneObjectKind::ScenePath` and
  `SceneRegistry::PathEntities()` are already added. Nothing further is needed in any of them.
- Everything else under `src/Renderer/Path/` - S1 to S5, done and green.
- `premake5.lua`, `src/Presentation/`, `src/App/`, `src/Domain/`, `src/IO/`, `src/Renderer/OpenGl/`.

## Acceptance criteria
1. `PathStore::Insert` returns false and changes nothing for an unset id or an id already present;
   true otherwise, and the new entry starts at revisions `{1, 1}`.
2. `Find` returns the inserted path by id and nullptr for an unknown one. `Contains`, `Size`, `Empty`
   agree with it.
3. `Erase` removes exactly that path, returns false for an unknown id, and does not reorder the rest:
   after inserting A, B, C and erasing B, `Ids()` is `{A, C}` and `At(1)` is C.
4. `MutateGeometry` bumps only `geometry`; `MutateStyle` bumps only `style`; both leave every other
   path's revisions untouched. Both return false and do not call the callback for an unknown id.
5. `RevisionsFor` returns `{0, 0}` for an unknown id, and `IsValid()` is false for it and true for
   every live path.
6. A copy of a store is deep: mutating the copy changes neither the original's paths nor its revisions,
   and the reverse. Assert it through `Find`, not through pointer identity.
7. `Visit` sees every path exactly once in `Ids()` order.
8. `PathCaches::Find` returns nullptr for an unknown id, for a key differing in `revisions.geometry`,
   in `revisions.style`, in `bindingSourceRevision` and in `lodBucket` - one test per field, because a
   key that ignores one field is the bug this stage exists to prevent.
9. `Store` under a key that is already present replaces the entry rather than adding one: `Size()` stays
   1 for one id across ten stores under ten different keys, and only the last key hits.
10. `Store` with an invalid id stores nothing (`Size()` unchanged) and the returned reference is
    readable (an empty `CachedPathGeometry`).
11. `Erase` drops one id and leaves the others; `RetainOnly` keeps exactly the listed ids and drops
    every other; `Clear` empties it. `RetainOnly({})` is equivalent to `Clear()`.
12. `PathCaches` is move-constructible and move-assignable and NOT copy-constructible - assert with
    `static_assert(!std::is_copy_constructible_v<PathCaches>)` and the matching positives.
    `PathSystem` is likewise not copyable.
13. `PathSystem::ErasePath` removes the path from the store AND its entry from the cache; erasing an
    unknown id returns false and touches neither.
14. `PathSystem::ReplaceStore` installs the new store and leaves the cache empty, even for ids the new
    store also contains. The header says why - do not "optimise" it into a `RetainOnly`.
15. `PathSystem::Clear` empties both.
16. `SceneSystem::EnsurePathSystem` creates the system on the first call and returns the same object on
    the second (compare addresses), and the paths inserted through the first call are still there.
17. `AppendScenePath` allocates a fresh id even when the incoming path already carries one, the returned
    id resolves in the store, and two appends of the same path produce two different ids.
18. After `SyncLabelEntities`, the registry holds exactly one `ScenePath` entity per stored path, with
    `sourceIndex` equal to the store position and the path's own `SceneObjectId` - and a second
    `SyncLabelEntities` with the store unchanged yields the same ids (identity survives a resync). A
    window with a null `paths` member syncs exactly as it does today.
19. Mirror sync of a window with paths does not disturb the arrow/label/orbital entities: an existing
    `SceneSystemTests` case for those still passes unchanged, and the per-kind entity vectors have the
    expected sizes.
20. `CaptureSceneObjectsSnapshot` -> mutate the store (insert one path, mutate another) ->
    `RestoreSceneObjectsSnapshot` restores exactly the captured set, by id and by content, and the
    window's cache is empty afterwards.
21. A snapshot captured from a window with a null `paths` member restores into an empty store and does
    not crash.
22. The export-preview copy is independent: after
    `PopulateExportPreviewState`-style `ReplaceStore(source.paths->Store())`, mutating the source store
    does not change the preview's, and neither system shares a cache (assert by storing into one cache
    and finding nothing in the other).
23. A `std::vector<RendererWindowState>` that reallocates (reserve 1, push two windows) keeps the
    `PathSystem` alive at the SAME address - record the address before the reallocation and compare
    after. This is the whole reason the member is a `Unique`.
24. Full Release test suite green except the two permanent skips
    (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
    `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`).

## Constraints
- Layer: `src/Renderer/` and `tests/` only. `Renderer` is the documented exception-free zone - no
  `throw`, no exceptions on any path; failures are return values.
- No `std::thread`. No OpenGL and no camera in any of the three new `.cpp` files: `PathStore`,
  `PathCaches` and `PathSystem` must stay unit-testable without a GL context, and the cache must not
  learn what a GL buffer is (that is S7's problem, and it hangs off the cache rather than inside it).
- Correctness must not depend on eviction. The key is what makes a stale entry a miss; `Erase`,
  `RetainOnly` and `Clear` exist for memory and dead ids. Do not add a "dirty" flag next to the
  revisions - two ways to say the same thing is how they drift apart.
- Do not add `selectedScenePaths`, a `scenePaths` vector on `RendererWindowState`, a second path
  container, or any picking/outliner support. S6 is ownership only; selection and UI are S8 and S15.
- `.cpp` files stay under ~500 lines.
- Style: tabs, `#include "Core/dspch.hpp"` first, anonymous namespace for helpers, `[[nodiscard]]`.
- Tests: GoogleTest, namespace `DefectStudio::Tests`, matching `tests/Renderer/Path/PathEvaluatorTests.cpp`.
