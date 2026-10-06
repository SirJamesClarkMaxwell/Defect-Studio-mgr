# Task 41 S9: persistence and v1 migration

## Goal
Paths survive save and load. `scene_objects.yaml` becomes format version 2 and gains a `ScenePath`
entry kind; a file from a future version is rejected before any of it is interpreted; the first v2
write leaves a `scene_objects.yaml.v1.bak` behind. A v1 arrow can be converted to a path with a
report naming everything the conversion lost.

## Files to create or change
- `src/IO/SceneObjectsIO.cpp` - parse and serialize the `ScenePath` entry kind, enforce the
  future-version guard, implement `WriteBackupOnce` and call it from `Save`.
- `src/IO/SceneObjectsYaml.{hpp,cpp}` - the path node/segment/style YAML lives here, like the
  orbital and plane shapes already do. `SceneObjectsIO.cpp` is at 506 lines and must not grow much.
- `src/Renderer/Scene/ScenePathPersistence.cpp` (new) - implements the header, already written.
- `src/Renderer/Scene/SceneObjectPersistence.cpp` - extract paths from the window into
  `PersistedScenePath`, and build them back on apply.
- `tests/IO/SceneObjectsPathIOTests.cpp` (new) and
  `tests/Renderer/Scene/ScenePathPersistenceTests.cpp` (new).
- `tests/IO/SceneObjectsV1Fixtures.hpp` (new) - the literal v1 YAML documents, as raw string
  literals. The plan called for files under `tests/fixtures/scene_objects_v1/`; raw literals carry
  the same text with the same fidelity and need no test-time file lookup, which no test in this
  repo currently has. Keep every fixture byte-for-byte a v1 document - do not "tidy" them.

## Files that must NOT be touched
- `src/IO/SceneObjectsIO.hpp` and `src/Renderer/Scene/ScenePathPersistence.hpp` - the contract. If a
  signature is wrong, stop and say so.
- Everything under `src/Renderer/Path/` - S1-S8 are frozen. Read it, do not edit it.
- `src/Renderer/OpenGl/**`, `src/Presentation/**`, `src/App/**`, `src/Domain/**`.
- Any existing test file.

## Out of scope (deliberate, do not add)
- Routing the migration into the live load path. S15 is the cutover: it turns the dev switch on and
  points the loader at the migration. Until then a v1 file still loads its arrows as arrows, and
  `MigrateArrowToPath` is a tested function nothing calls in production. Converting arrows to paths
  now would take away every arrow's editing UI, which paths do not have until S11.
- Resolving `PathBinding::ObjectOrigin` across objects. `PersistedPathBinding::objectPersistKey`
  round-trips, but `BuildScenePath` has no way to turn a persistKey into the SceneObjectId of an
  object that may not be loaded yet, so it degrades an ObjectOrigin binding to `Free` and warns.
  S14 owns binding wiring and gives it the second pass it needs.

## Acceptance criteria
1. `SceneObjectsIO::kFormatVersion == 2` and a serialized file states `formatVersion: 2`.
2. A v2 file with one path of each segment kind (Line, Cubic, Arc) round-trips through
   `Serialize` then `Parse` with every node position, handle position, handle type, plane normal
   and sweep preserved.
3. Every style field round-trips: profile, width, join, cap, radialSegments, colour, alpha, the
   whole dash block, a gradient with three stops, both decorations with their two scales, depthMode.
4. `Parse` on a document with `formatVersion: 3` returns false, sets `outError`, and leaves
   `outFile` empty - nothing from that file is interpreted.
5. `Parse` on a v1 document (`formatVersion: 1`) still loads its arrows, orbitals, planes, labels
   and pins exactly as before.
6. A path entry missing `nodes`, with fewer than two nodes, or whose segment count is not
   `nodes.size() - 1`, is skipped with one `scene_objects.entry_skipped` warning and does not abort
   the rest of the file.
7. `WriteBackupOnce` on a directory holding a v1 `scene_objects.yaml` creates
   `scene_objects.yaml.v1.bak` with the original bytes; a second call does not overwrite it, and a
   call in a directory with no file or with a v2 file succeeds and creates nothing.
8. `Save` into a directory whose backup cannot be written returns false and leaves the existing
   `scene_objects.yaml` byte-identical.
9. `BuildScenePath` allocates fresh element ids: every node, segment and handle id is non-zero and
   unique within the path, and `nextElementId` is past all of them.
10. `BuildScenePath` -> `ExtractPersistedScenePath` -> `BuildScenePath` is stable: the second path
    equals the first in everything but element ids.
11. A `CopyPosition` binding whose atom reference resolves comes back bound with its buffer; one
    that does not resolve comes back `Free` with a warning and the node's stored position intact.
12. An `ObjectOrigin` binding loads as `Free` with a warning (see "Out of scope").
13. `MigrateArrowToPath` on a straight two-point v1 Line produces two nodes, one Line segment and
    a Round profile.
14. A two-point arrow with a control point produces one Cubic whose handles are exactly
    `P + 2/3 (Q - P)` at each end, with both handle types Free.
15. A five-point arrow produces five nodes and four Line segments; a five-point arrow that also
    carries a control point produces the same and one warning.
16. Arrow2D Billboard migrates to `CameraFacing` + `AlwaysOnTop`; Arrow2D FixedPlane to `Flat` +
    `AlwaysOnTop` + a warning; Arrow3D and Line to `Round` + `DepthTest`.
17. `style.width` is exactly twice the v1 `shaftWidth`.
18. All six tips map per the header's table, and both decoration scales are the v1 absolute
    head size divided by the new full width.
19. A v1 arrow with `useGradient` produces an enabled gradient with exactly two stops at positions
    0 and 1 carrying the v1 start and finish colours; `alpha`, `dashed`, `dashLength` and
    `gapLength` all carry over.
20. A v1 arrow with a start and an end anchor atom produces `CopyPosition` bindings on the first
    and last node only, each carrying `atomBuffer`.
21. `curveSegments` other than the v1 default, and a non-zero `outlineWidth`, each produce one
    warning naming what was lost; neither blocks the migration.
22. An arrow with fewer than two points, or any non-finite coordinate, is rejected with a
    `StructuredError` and no partial path.
23. Every migrated path passes `ValidatePath` with no diagnostics.
24. The full Release suite is green with the two permanent `DS_PYTHON_CAPI_AVAILABLE=0` skips and
    no other skips.

## Constraints
- Layer boundary: IO parses DTOs and nothing else. Every judgement about what a v1 arrow meant -
  which profile, which tip, what was lost - lives in `Renderer/Scene/ScenePathPersistence.cpp`.
  `src/IO/` must not include anything from `Renderer/Path/`.
- `Renderer` is the exception-free zone: no `throw`. Errors are `StructuredError` / `Result<T>`.
- `.cpp` files stay under ~500 lines. `SceneObjectsIO.cpp` is already at 506 - put the path YAML in
  `SceneObjectsYaml.cpp` and split further if it approaches the limit.
- Warning codes: reuse `scene_objects.entry_skipped` for a skipped entry. New codes go in the
  `scene_objects.` namespace: `future_format_version`, `path_binding_unresolved`,
  `path_migration_lossy`.
- `glm::isfinite` / `glm::all` are unavailable - check finiteness component-wise with `std::isfinite`.
  GLM constructors are explicit: `glm::vec3(someVec2)` does not compile.
- Reuse the helpers in `SceneObjectsYaml.hpp` (`Vec3`, `EmitVec3`, `ParseAnchors`, `EmitAnchors`)
  rather than writing new ones.
- Run `scripts/Windows/GenerateProjects.bat` after adding the new files. Do not build - the sandbox
  cannot build this project; this session builds and tests.

## Manual round (user, after the stage)
Open the real project(s) with old arrows and read the migration report. Nothing should convert yet
in normal use - the point of the round is that saving and reopening a project with dev paths in it
brings them back unchanged, and that `scene_objects.yaml.v1.bak` appears exactly once.

## Implementation notes (2026-09-26)
- Criterion 8 is implemented but not covered by a test: forcing a backup write to fail needs
  filesystem fault injection this repo has no harness for. `Save` calls `WriteBackupOnce` first and
  returns on failure before touching the temp file, so the existing `scene_objects.yaml` is
  untouched on that path by construction.
- `Serialize` stamps `kFormatVersion` rather than echoing `SceneObjectsFile::formatVersion`. The
  field stays meaningful on read; a write is always current-version.
- Path persist keys are assigned in `EnsureScenePersistKeys` like every other scene object kind, not
  minted during extraction - a key minted per save is an identity nothing can reference across a
  reload.
