# Task 41 transform-1: a path has a transform, a handle has an offset

## Goal

A `ScenePath` gains its own position, rotation and scale, and its authored node positions become
local to it. This is what the Properties panel's Location / Rotation / Scale fields will show, and
what makes `SceneTransformLocalBasis` able to return a basis for a path-only selection instead of
`nullopt`.

The plan is `docs/work/project/plans/2026-09-26-path-object-transform.md`. This is step 1 of four;
persistence, the panel and the gizmos follow separately.

## The contract, already written

- `src/Renderer/Path/PathTypes.hpp` - `PathTransform` (position, quaternion rotation, scale),
  `ScenePath::transform` defaulting to the identity, and `PathHandle::offset` replacing
  `PathHandle::position`.
- `src/Renderer/Path/PathBindingResolver.hpp` - the rule the whole thing rests on, above
  `ResolveNodePositions`. Read it before writing anything.

In short:

    Free node   -> world = transform * node.position      (authored, local)
    Bound node  -> world = whatever the binding resolves to, UNTRANSFORMED

A handle's offset is rotated and scaled by the transform, then anchored at whatever world position
its own node resolved to.

## Why the tree does not compile right now

`PathHandle::position` is gone and roughly a dozen files still name it. That is this task's work,
not an accident. Do not put the field back.

## Files to create or change

Start from the compiler errors; this list is what a read of the tree suggested and may be
incomplete:

- `src/Renderer/Path/PathBindingResolver.cpp` - apply the transform, per the contract
- `src/Renderer/Path/PathEvaluator.cpp` - handles are offsets now
- `src/Renderer/Path/PathHandleRules.cpp` - Aligned/Vector/Auto rules operate on offsets, which
  makes most of them simpler, not harder
- `src/Renderer/Path/PathTopology.cpp`, `PathCommands.cpp` - wherever a handle is read or written
- `src/Renderer/Scene/SceneTransformPaths.cpp` - `G`/`R`/`S` compose onto `path.transform` instead
  of walking every node and handle
- `src/Renderer/Scene/SceneTransform.cpp` - `SceneTransformLocalBasis` returns the path's own
  rotation for a path-only selection, instead of `nullopt`
- `src/Presentation/Panels/ScenePathOperations.cpp`, `ScenePathDevMenu.cpp` - call sites

## Files that must NOT be touched

- `src/Renderer/Path/PathTypes.hpp`, `src/Renderer/Path/PathBindingResolver.hpp` - the contract
- `src/Renderer/Scene/ScenePathPersistence.cpp`, `src/IO/` - persistence is step 2 and has its own
  migration. Leave the format alone; a path loaded today gets the identity transform, which is
  correct and is exactly why the migration is free.
- `src/Presentation/Panels/ScenePathEditorWidget.cpp` - the panel is step 3
- `src/Renderer/Path/PathStrokeMesher.cpp`, `PathDecorationMesher.cpp` - they consume resolved
  positions and must not learn about the transform
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. With the identity transform, `ResolveNodePositions` returns exactly what it returned before this
   task, for free and bound nodes alike. This is the regression guard and it is what makes every
   existing file load unchanged.
2. A free node's resolved position is `transform * node.position`.
3. A bound node's resolved position is the binding's world position, unaffected by the transform -
   translate, rotate or scale the path and it does not move.
4. A path whose nodes are all bound does not travel when translated. It deforms. Pin this; it looks
   like a bug and it is the correct behaviour.
5. A handle resolves to its node's resolved position plus its offset rotated and scaled by the
   transform, for both free and bound nodes.
6. A non-uniform scale is applied consistently to positions and handle offsets, so the curve keeps
   its shape under the same scale rather than shearing differently from its tangents.
7. `SceneTransformPaths` composes onto `path.transform`. Translating no longer rewrites node
   positions, and undo of a drag restores the transform.
8. `SceneTransformLocalBasis` returns the path's rotation for a path-only selection, so
   `CycleConstraint` stops falling back to Global.
9. Handle rules - Aligned, Vector, Auto - behave as they did, now expressed on offsets.
10. Nothing outside the resolver reads `path.transform`. Grep for it: the mesher, the tessellator,
    picking and the caches must not name it.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` stays exception-free.
- `.cpp` files stay under ~500 lines.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- Do not touch, revert, clean or delete anything else in the worktree, tracked or untracked. Other
  sessions are working in parallel in different directories.
- If you believe a contract is wrong, stop and say so instead of changing it.
