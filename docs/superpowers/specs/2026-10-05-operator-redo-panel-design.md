# Operator redo panel and parameterized C_n rotation arrows

Date: 2026-10-05
Status: design approved, not yet planned

## Intent

Three things the user asked for, in one design because they share a mechanism:

1. The curved C_n arrow for **two** atoms draws a shallow arc lying in the plane between
   them. The user expects an arrow that **wraps around the bond axis** — the C_2 operation
   is a rotation about the bond, so the arrow should encircle it. The n >= 3 case (arrows
   around the defect z axis) already looks right and does not change.
2. The arrows must be selectable and editable, with an easy way to rotate them about the
   defect axis to control where they sit.
3. A small panel should appear after the operation, like Blender's "Adjust Last Operation",
   to tune radius, curvature, heads and colors for all n arrows at once.

Point 2 is partly a misunderstanding: curved arrows are plain `ScenePath`s and already
appear in `RendererWindowState::selectedScenePaths`, so they already select and already
accept G/R/S. What genuinely does not exist is rotation constrained to a chosen axis —
`CreateRendererAlignAxisCommand` only snaps an axis onto an axis. Confirm the selection
part against the running app before implementing anything for it.

## Decisions taken

| Question | Decision |
|---|---|
| C_2 rotation axis for two atoms | The bond line. Arc in the plane perpendicular to it, centred on the bond midpoint. |
| Default arc shape | Radius = largest of the two atom radii x 1.4; sweep 270 degrees. Both are panel options, with "fraction of bond length" as the alternative radius rule. |
| Panel scope | A general mechanism, as in Blender: every Add operation can register parameters and get the same panel. |
| Change model | Undo-and-re-run, Blender style. One undo entry regardless of how many times a slider moves. |
| Rotation about an axis | Both: a parameter in the panel for creation time, and a modal R with axis constraint for later edits. Both write the same object rotation parameter. |

## Architecture

### Operator registry

An *operator* is a named scene operation that can be run again: a parameter schema (name,
type, range, default), an `Execute(params) -> created object ids` entry point, and a label
for the history. Operators are registered alongside the existing scene operations and
invoked through `CommandRegistry` / `CommandService`, so no layer is bypassed.

The schema is data, not code: the panel renders widgets from it and does not know what a
curved arrow is. That is what makes the mechanism general — a later operation registers its
parameters and gets the panel for free.

### Redo panel and the undo contract

The existing `SceneObjectsSnapshotCommand` already does the hard part. It captures the
window's scene objects whole and is pushed onto the global `UndoStack` with
`PushExecuted` **before** the edit is applied; its `Undo` captures the current state as the
redo state and restores the `before` snapshot.

That gives the re-run model without any new undo API and without popping entries:

1. On the **first** invocation, capture `before` and push one `SceneObjectsSnapshotCommand`.
2. On **every** parameter change, restore `before` into the window and run the operator
   again with the new parameters. The stack is not touched.
3. `Ctrl+Z` restores `before`, whatever happened in between, because the entry holds the
   state from before the first run.

Popping the last entry was the obvious design and it is the wrong one: it would fight the
redo branch and the clean-index bookkeeping in `UndoStack`. Restoring a held snapshot does
not.

**Invalidation.** The panel must close as soon as anything else touches the history,
otherwise restoring `before` would silently discard that work. `UndoStack::GetUndoDepth()`
already exists: record it after the push, compare on every frame, close the panel when it
differs. No new API.

The panel also closes when another operator runs, which is the Blender behaviour.

### Parameterized C_n arrow

`AddCurvedArrowThroughSelectedAtoms` currently takes only `RendererWindowState &` and hides
its shape constants in the `.cpp` (sagitta ~0.13 of the chord). It gains a parameter struct:

- axis mode: bond / defect z / auto (auto = bond for exactly two atoms, defect z otherwise)
- radius rule: atom-relative multiplier (default 1.4) or fraction of bond length
- sweep in degrees (default 270)
- rotation offset in degrees about the axis (default 0)
- decoration kind, color, stroke width, curvature

Defaults reproduce the agreed look for two atoms. For n >= 3 the defaults reproduce today's
behaviour around the defect z axis, so that case is unchanged by construction.

### Axis-constrained modal rotate

Extend the existing modal R with an axis choice that includes the defect axes. It writes the
same rotation parameter the panel field writes, so the two routes cannot drift apart.

## Testing

- Parameters to geometry: the arc lies in the plane perpendicular to the chosen axis, at the
  requested radius, spanning the requested sweep, with the requested winding.
- Defaults clear the spheres: with the default radius rule the arc does not intersect either
  atom's sphere.
- Re-run leaves one entry: after five parameter changes `GetUndoDepth()` has grown by one,
  and one `Ctrl+Z` returns the scene to the pre-operation state.
- Panel invalidation: an unrelated undoable edit closes the panel and leaves that edit intact.
- n >= 3 regression: the existing three-atom cycle test still passes unchanged.

A passing geometry test proves only what it measures — assert the arc's plane and winding
explicitly rather than inferring them from a vertex count.

## Boundaries

- `Domain` untouched.
- Operator registry and panel in `Presentation`; arc geometry in `Renderer/Path`.
- Execution through `CommandRegistry`; undo through the existing `UndoStack` entry.
- New `.cpp` files stay under ~500 lines.

## Out of scope

- Registering operations other than the C_n arrow with the new registry. The mechanism is
  built general; wiring the other Add entries happens when they are needed.
- Any change to the n >= 3 arrow cycle beyond routing it through the parameter struct.
- The v1 `SceneArrow` migration size bug (`ScenePathPersistence.cpp:376` copies pixel widths
  into world units). Separate defect, separate fix.
