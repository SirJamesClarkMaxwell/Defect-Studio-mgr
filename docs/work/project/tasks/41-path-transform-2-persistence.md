# Task 41 transform-2: the transform is saved, and a path's origin is its centre

Step 2 of `docs/work/project/plans/2026-09-26-path-object-transform.md`.

## Goal

A path's transform survives save and reload, and a path's origin sits at the centre of its nodes so
the Location field the panel will show in step 3 says where the path actually is.

## The contracts, already written

- `src/IO/SceneObjectsIO.hpp` - `PersistedScenePath::transformPosition`, `transformRotation`
  (a quaternion, xyzw), `transformScale`. Optional and additive: absent means the identity, and the
  format version does not move.
- `src/Renderer/Path/PathTopology.hpp` - `MovePathOriginToCentre`, with the rule for when it runs
  and, just as importantly, when it must not.

## Why absent means the identity, and why that is enough

Under the identity a local position equals the world position that files written before this
stored. So an old file loads and renders identically with no migration step at all - the block is
simply missing and the default is correct.

## Where `MovePathOriginToCentre` is called

Exactly two places:

- when a path is created, so a new path's Location reads where it is;
- when a path is loaded from a file that carried **no** transform block, so old projects gain a
  meaningful Location the first time they are opened.

A file that DOES carry a transform block already has its origin where its author put it. Do not
re-centre it on load - that would move the origin every time the project was opened.

Never after an edit. Blender does not move an origin when a vertex moves, and an origin that
wandered on every drag would make Location meaningless in the other direction.

## Files to create or change

- `src/Renderer/Path/PathTopology.cpp` - `MovePathOriginToCentre`
- `src/Renderer/Scene/ScenePathPersistence.cpp` - read and write the block, and the load-time
  centring for files without one
- `src/IO/SceneObjectsYaml.cpp` - the keys. Use snake_case, matching `ribbon_normal` and
  `start_decoration_filled`: `transform_position`, `transform_rotation`, `transform_scale`.
- `src/Presentation/Panels/ScenePathDevMenu.cpp` and wherever else a path is constructed - call
  `MovePathOriginToCentre` on creation

## Files that must NOT be touched

- `src/IO/SceneObjectsIO.hpp`, `src/Renderer/Path/PathTopology.hpp` - the contracts
- `src/Renderer/Path/PathBindingResolver.{hpp,cpp}` - the resolver already applies the transform
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. A path with a non-identity transform round-trips through save and load exactly.
2. A file with no transform block loads with the identity, and its resolved geometry is unchanged
   from before this task.
3. The format version stays where it is and no existing key changes meaning.
4. `MovePathOriginToCentre` leaves the resolved geometry identical: the centroid is added to the
   transform and subtracted from every node.
5. It is idempotent in effect - running it twice leaves the geometry where it was, and the second
   run moves nothing because the centroid is already zero.
6. A path with no nodes is left alone.
7. A path with bound nodes keeps resolving to the same world positions: the authored fallback moves
   with the rest, and the bindings are unaffected because they never read the authored position
   while they resolve.
8. A newly created path has its origin at its centre, so `transform.position` is where the path is.
9. A path loaded from a file WITH a transform block is not re-centred.
10. A rotation round-trips through the quaternion keys without normalising to a different but
    equivalent representation - `q` and `-q` are the same rotation, and a test that assumes
    otherwise is testing the serializer's sign convention, so make the convention explicit if you
    depend on it.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` stays exception-free.
- `.cpp` files stay under ~500 lines.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked.
  Another session is fixing the gizmo pivot at the same time, in `SceneTransform.cpp` and
  `ViewportModalTransform.cpp`.
