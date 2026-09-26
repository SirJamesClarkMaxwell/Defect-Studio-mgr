# Paths get an object transform

Decided 2026-09-26 with the user, who chose Blender's Object Mode model over the Edit Mode one
after being shown both. This is the design, written before any code moves.

## What changes

A `ScenePath` gains its own transform - position, rotation, scale - and its authored node positions
become **local** to it. Today every node position is in world space and there is no transform at
all, which is why the Properties panel has no T/R/S fields, why `SceneTransformLocalBasis` returns
`nullopt` for a path-only selection (`SceneTransform.cpp:238-246`), and why "Local" orientation
silently falls back to Global.

## Why this is smaller than it looks

`ResolveNodePositions` (`PathBindingResolver.hpp:32`) is already the single place that turns
authored data into world positions. Everything downstream - tessellation, stroke meshing, picking,
the caches - consumes its output and never reads `node.position` directly. The transform therefore
slots into exactly one function, and nothing below it learns that paths can be transformed.

Two consequences follow for free:

- **Migration is a no-op.** A default transform is the identity, and under the identity a local
  position equals the world position it used to be. Every existing `scene_objects.yaml` keeps
  loading and keeps rendering identically. The format gains an optional block; the version does
  not move.
- **Undo gets cheaper.** A G/R/S drag currently rewrites every node and every cubic handle in the
  snapshot. It becomes a change to nine floats.

## The part that is not free: bindings

`PathBinding` is a variant of `Free`, `CopyPosition` (an atom), `BondMidpoint` (two atoms) and
`ObjectOrigin` (`PathTypes.hpp:49-75`). Only `Free` carries an authored position. The other three
resolve to a **world** position taken from something else in the scene.

A bound node therefore cannot be transformed by the path's transform. It is pinned to an atom; that
is the whole point of binding it. So a single path can have nodes in two different spaces at once,
and that has to be explicit rather than accidental:

    free node   ->  world = pathTransform * node.position
    bound node  ->  world = whatever the binding resolves to, untransformed

This is the first acceptance criterion of whatever task implements it, not a footnote.

**The open question is cubic handles.** `CubicBezierSegmentData` carries `startHandle` and
`endHandle`. A segment between a free node and a bound node has one handle that should follow the
transform and one that should follow the atom. Decide this before implementing, not during:
either handles become offsets relative to their own node - which makes the answer automatic and is
probably right - or they stay absolute and each one has to be told which space it is in.

## What a transformed path means for G/R/S

`SceneTransformPaths` currently walks every node and every handle and adds a delta
(`SceneTransformPaths.cpp:29-50`). It becomes composition onto the path's transform instead.

A path whose nodes are all bound does not move when it is translated - the bindings win, and the
path deforms rather than travelling. That is the correct behaviour and it will look surprising the
first time; it should be what a test pins, so nobody later "fixes" it.

## What it unlocks

- Numeric Location / Rotation / Scale in Object Properties, as three persistent fields, because
  now there is something persistent to show.
- `SceneTransformLocalBasis` returns the path's own rotation, so Local orientation starts working
  without inventing a synthetic frame from the first node and the tangent.
- The rotate and scale gizmos the user asked for - a trackball sphere with three arcs, and scale
  handles - become a view onto a real transform rather than an accumulator of node deltas. They
  ship with the numeric panel; the user chose to do both together.

## Sequencing

Not one task. At least four, in this order, each its own commit:

1. **The model.** `ScenePath::transform`, the resolver applying it to free nodes only, the handle
   decision, and `SceneTransformPaths` composing onto it. Tests pin the two-space rule and the
   identity equivalence.
2. **Persistence.** The optional transform block, absent meaning identity. No version bump.
3. **The panel.** Location / Rotation / Scale fields, drag-coalesced like every other path edit,
   plus `SceneTransformLocalBasis` for paths.
4. **The gizmos.** Trackball rotate and scale handles, over the existing modal transform.

Step 1 is the only one with real risk. Steps 2 to 4 are mechanical once it lands.

## Relationship to the other open work

This sits behind the current S11 defect round and behind the renderer-windows-as-tabs plan
(`2026-09-26-renderer-windows-as-tabs.md`), which is itself queued. Three large threads are now
open at once; they should not be interleaved.
