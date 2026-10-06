# Task 50a: bound path nodes follow their atoms everywhere

First slice of PathSystem stage S14 (`docs/work/project/plans/2026-09-20-path-system-implementation.md`).

## Goal

A path node can already carry a binding (`PathBinding::CopyPosition`, `BondMidpoint`,
`ObjectOrigin`) and `ResolveNodePositions` already resolves it - but every consumer except the scene
mirror passes an empty `BindingContext{}` (the S7 placeholder), so a bound node is drawn, picked,
outlined and transformed at its authored fallback position instead of at its atom. After this task
every consumer resolves against the window's live context, and the render cache is invalidated when
a bound node's target moves.

## The contract, already written

- `src/Renderer/Path/PathBindingResolver.hpp` - `BindingSourceRevision`. **Read its comment.**
- `src/Renderer/Path/ScenePathPicking.hpp` - `PickFrontmostScenePath` now takes the context.
- `src/Renderer/OpenGl/OpenGlRendererBackend.hpp` - `PathRenderInput::bindings`.
- `tests/Renderer/Path/PathBindingSourceRevisionTests.cpp`.
- `tests/Renderer/Path/ScenePathPickingTests.cpp`, test
  `BoundNodeIsPickedWhereItsAtomIsNotWhereItWasAuthored` (the existing calls there were updated to
  pass `BindingContext{}`; they keep their meaning).

## What already exists - reuse it

- `SceneSystem::MakePathBindingContext(const RendererWindowState &)`
  (`src/Renderer/Scene/SceneSystem.cpp`) - THE live context: atoms, radii, object origins. Every
  consumer uses it. Do not write a second one.
- `PathEvaluationKey::bindingSourceRevision` (`src/Renderer/Path/PathCaches.hpp`) - the slot already
  exists and is always 0 today.

## Files to create or change

- `src/Renderer/Path/PathBindingResolver.cpp` - implement `BindingSourceRevision`.
- `src/Renderer/Path/ScenePathPicking.cpp` - resolve with the passed context.
- `src/Renderer/OpenGl/OpenGlPathRenderer.cpp` - resolve with `*input.bindings` (empty context when
  null) and put `BindingSourceRevision(path, resolved)` into the cache key instead of the literal `0`.
  Remove the "S7 resolves against an empty binding context" comment.
- `src/Renderer/RendererLayer.cpp` (~line 500) - build the context with
  `SceneSystem::MakePathBindingContext(windowState)` for the window being rendered (this is also the
  export path: it must be the state actually rendered, e.g. the export preview copy, never a
  different window) and pass its address in `PathRenderInput`.
- `src/Presentation/Panels/ViewportScenePathInteraction.cpp` (two `BindingContext{}` sites, and the
  `PickFrontmostScenePath` call), `ViewportPathOverlay.cpp`, `ViewportRegionPathSelect.cpp` - use
  `SceneSystem::MakePathBindingContext(windowState)`.
- `src/Renderer/Scene/SceneTransformPathElements.cpp` (~line 282) - use the `bindingContext`
  argument the function already receives instead of `BindingContext{}`, and rewrite the comment
  above it: markers and pivot now agree because both use the live context.
- Update the `ScenePathPicking.hpp` comment paragraph "Node positions are resolved exactly the way
  the render pass resolves them - against an empty BindingContext..." to say they use the caller's
  live context, the same one the render pass gets.

Then regenerate projects (new test file): `DS_TOOLSET=msc-v143` + `scripts\Windows\GenerateProjects.bat`.

## Files that must NOT be touched

- The contract headers above and both test files.
- `PathBindingResolver.cpp`'s `ResolveNodePositions` - it is correct; only add the new function.
- `src/Renderer/Path/PathCommands.*`, `PathTopology.*`, `PathEditSession.*`.
- Everything bevel-related (`PathSolidMesher*`, `PathDecorationMesher*`, `*Bevel*`).
- `src/Presentation/Panels/ScenePathEditCommands.*`, `ScenePathOperations.*`.
- `SceneSystem::RefreshAnchoredSceneArrows` and every other legacy `SceneArrow` path.

## Acceptance criteria

1. Release build of DefectStudio and DefectStudioTests succeeds.
2. `DefectStudioTests --gtest_filter=PathBindingSourceRevisionTests.*:ScenePathPickingTests.*` passes.
3. Full suite passes with only the two known skips.
4. `grep -rn "BindingContext{}" src` finds no hit outside `src/Renderer/Path/` default arguments
   (there should be none at all in `src/Presentation` and `src/Renderer/OpenGl`).

## Constraints

- `Renderer` must not include anything from `Presentation`.
- The `BindingContext` lambdas capture the window state by reference: build the context in the same
  scope that uses it, never store it in a member or a cache.
- No exceptions; no new revision counter on `RendererWindowState` - `BindingSourceRevision` replaces
  the need for one.
