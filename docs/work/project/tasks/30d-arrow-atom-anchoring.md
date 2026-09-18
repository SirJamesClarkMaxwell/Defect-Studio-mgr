# Task 30d: an arrow anchored to two atoms, with a live buffer

Branch: `task/30d-arrow-atom-anchoring`, on top of `task/30c-scene-object-deletion-and-buffer`.

## Goal

A scene arrow drawn between two atoms stays attached to them. Its endpoints are re-derived from
the atoms' current positions, so moving an atom moves the arrow, and changing the arrow's `Bufor`
moves its ends immediately instead of only at the moment it was drawn. Dragging an endpoint by hand
detaches that one end and leaves the other attached.

User-visible symptom this closes: "buff działa, ale tylko jako stała wartość - raz ustawię
i strzałka po prostu jest, nie reaguje na zmianę buff." The buffer is currently a session-wide
static read once, at creation (`GetSceneArrowAtomBuffer`, `SceneArrowOperations.cpp:125`). After
this task it is a per-arrow property.

## This is a copy of an idiom that already exists - do not invent a second one

`SceneOrbital` and `ScenePlane` are already anchored to atoms and everything needed is built:

- The field: `std::vector<std::size_t> anchorAtoms` on the object
  (`RendererWindowState.hpp`, `struct ScenePlane` - read its comment, it states the lifetime rules).
- Resolution: `ResolveAnchor(storedValue, anchorAtoms, slot, structure)` in
  `SceneOrbitalGeometry.cpp:109-110` - falls back to the stored coordinate when the slot holds no
  resolvable atom. That fallback is exactly what a half-detached arrow needs.
- Persistence: `PersistAtomReferences` / `ResolveAtomReferences` with `PersistedAtomRef`
  (`SceneObjectPersistence.cpp:241,260,369,397`, `SceneObjectsIO.hpp:74,134`) and the `anchorAtoms`
  YAML sequence (`SceneObjectsYaml.cpp:83,112,139,165,187`). A `PersistedAtomRef` survives a
  structure edit better than a raw index; use it, do not persist bare indices.
- The UI: the anchored/free block with the `Odczep` button,
  `ObjectPropertiesPanelOrbital.cpp:61-96` and `ObjectPropertiesPanelSections.cpp:119-143`.

Mirror all five for `SceneArrow` - except the field's *shape*, which the Design section below
amends: an arrow needs two independently detachable ends and the dense `anchorAtoms` vector cannot
express that through a save. Everything else on this list is reused as it stands. If anything else
turns out not to fit an arrow, stop and say why rather than writing a parallel mechanism.

## Design

New fields on `RendererWindowState::SceneArrow`:

```cpp
// Which atom each end is anchored to, indices into structure.atoms. Empty optional = that end is
// a plain coordinate the user placed, which is what dragging it leaves behind. An index that no
// longer resolves is ignored, never clamped - the stored start/end coordinate stands in, the same
// degradation rule as ScenePlane::anchorAtoms.
std::optional<std::size_t> startAnchorAtom;
std::optional<std::size_t> endAnchorAtom;
// Gap at each anchored end, in that atom's own radii. 0 = the atom's centre, 1.0 = tangent to the
// drawn sphere, the 1.15 default leaves a visible gap. Was a session-wide static before task 30d.
float atomBuffer = 1.15f;
```

**Amended 2026-09-18, after the first dispatch stopped on this.** The original contract said to copy
`ScenePlane::anchorAtoms` - one `std::vector<std::size_t>` with a `kFreeArrowAnchor` sentinel in the
slot of a detached end. That does not survive a round trip and the dispatch was right to refuse it:
`PersistAtomReferences` drops every unresolvable entry and `ResolveAtomReferences` hands back a
dense vector, so `{kFreeArrowAnchor, 1}` reloads as `{1}` and a start-detached arrow comes back
anchored by its start to the wrong atom. The positional meaning is lost precisely because the
existing helpers were built for a variable-length set of anchors where no slot is ever empty.

An arrow is not that. It has exactly two ends and either may be free, so it gets two optionals
instead of a sparse vector. This is a different *shape*, not a second mechanism: `PersistedAtomRef`
and both helpers are still what does the work. Persist each end by calling
`PersistAtomReferences` with a zero-or-one element vector and `ResolveAtomReferences` on the way
back - an empty result is a free end, a one-element result is the anchor. Nothing in the orbital or
plane path changes, and the shared helpers keep the contract they already have.

**Where the endpoints get re-derived.** Do NOT change every consumer of `arrow.start`/`arrow.end` -
they are read by the renderer, picking, the gizmo, `SceneTransform` and the outliner, and rewriting
all of them is a much larger and riskier diff than this defect deserves. Instead keep
`start`/`end` as the resolved values and refresh them once per frame for anchored arrows only, in
the same pass that already refreshes label transforms (`SceneSystem::UpdateLabelTransforms` - check
whether arrows belong there or need a sibling call next to it; pick whichever keeps the call sites
that already exist, and say which you chose and why). Every existing consumer then keeps working
untouched.

Refresh = for each arrow with at least one anchored end, resolve that end from its atom,
then apply the buffer with the maths that is already in `MatchSceneArrowPositionToAtoms`
(`SceneArrowOperations.cpp:134`), including its anti-inversion `scale` clamp. Extract that maths so
it is shared and not copied - `MatchSceneArrowPositionToAtoms` should end up as the "snap now"
wrapper around the same helper the refresh uses.

**Creation.** `DrawSegmentAddItems` (`RendererPanelOrbitalMenu.cpp:156-175`) already matches the new
arrow to the two selected atoms. It should now also set `startAnchorAtom = first`,
`endAnchorAtom = last` and copy the menu's buffer value into `arrow.atomBuffer`. Keep `GetSceneArrowAtomBuffer` as the *default for new
arrows* only, and say so in its comment - it is no longer what an existing arrow reads.

**Detaching by drag.** In `ViewportSceneArrowInteraction.cpp` the drag writes `arrow.start` or
`arrow.end` under `sceneArrowDragTarget` (`DragTarget::Start` / `End` / `Both`). Reset the
matching optional when a drag actually moves an endpoint; `DragTarget::Both` frees both. Do not
clear both anchors for a single-end drag - the other end stays anchored. The
modal `G`/`R`/`S` transform path in `SceneTransform.cpp` moves endpoints too; it must detach the
same way, or an anchored arrow will snap back the next frame and look like the transform did
nothing.

**Properties panel.** Add the anchored/free block to the arrow section, the same shape as the
orbital's: show which atoms each end is attached to, an `Odczep` button, a button to attach the
selected two atoms, and the `Bufor` drag float now bound to `arrow.atomBuffer` (disabled when the
arrow is free, because it means nothing then).

## Files to create or change

- `src/Renderer/RendererWindowState.hpp` - the three new fields (two optionals and the buffer).
- `src/Presentation/Panels/SceneArrowOperations.cpp` / `SceneArrowEditorWidget.*` - the shared
  buffer maths, the per-arrow `Bufor` control, the anchor block.
- `src/Presentation/Panels/RendererPanelOrbitalMenu.cpp` - set the anchors at creation.
- `src/Presentation/Panels/ViewportSceneArrowInteraction.cpp` - detach on drag.
- `src/Renderer/Scene/SceneTransform.cpp` - detach on modal transform.
- `src/Renderer/Scene/SceneSystem.cpp` / `.hpp` - the per-frame refresh.
- `src/Renderer/Scene/SceneObjectPersistence.cpp`, `src/IO/SceneObjectsIO.hpp`,
  `src/IO/SceneObjectsYaml.cpp` - round-trip the two anchors and the buffer. YAML keys:
  `startAnchorAtoms` and `endAnchorAtoms`, each a sequence of zero or one entry emitted by the same
  `EmitAnchors`/`ParseAnchors` pair that `anchorAtoms` already uses, and `atom_buffer` for the
  float. The camelCase anchor keys and the snake_case float are both deliberate - each matches the
  neighbour it sits next to in that file rather than inventing a third convention.
- `tests/` - see Acceptance criteria.

## Files that must NOT be touched

- `src/Domain/` - none of this is domain logic.
- `src/App/` - no composition-root change.
- `premake5.lua` - no new project, no new build flag (still run `GenerateProjects.bat` if you add a
  file).
- The orbital and plane anchoring code you are copying from. Read it, reuse its helpers, change
  nothing in it. If a helper needs to be generalised to serve both, that is allowed - but say so in
  your report and keep the orbital/plane behaviour identical.

## Acceptance criteria

1. A GoogleTest anchors an arrow to two atoms, moves one atom, runs the refresh, and asserts the
   arrow's endpoint followed and still clears the sphere by the buffer.
2. A GoogleTest changes `atomBuffer` on an anchored arrow, runs the refresh, and asserts both
   endpoints moved, and that setting it to `0` puts them exactly at the atom centres.
3. A GoogleTest frees `startAnchorAtom` only, runs the refresh, and asserts `start` kept the
   hand-placed coordinate while `end` still tracks its atom. Then round-trips THAT arrow through
   the project file and asserts it comes back with the start still free and the end still anchored
   to the same atom - this is the case that stopped the first dispatch, so it must be pinned.
4. A GoogleTest round-trips a fully anchored arrow through `tests/IO/SceneObjectsIOTests.cpp`: both
   anchors and `atomBuffer` come back equal, and a project file written before this change (no such
   keys) loads as a free arrow with `atomBuffer == 1.15f`.
5. A GoogleTest covers the anti-inversion clamp on the refresh path: two atoms closer together than
   the requested gap leave a short arrow pointing the original way, never a reversed one.
6. `scripts/Windows/Build.bat --config Release` builds both targets with zero errors and zero
   warnings. 2 skipped tests expected (`DS_PYTHON_CAPI_AVAILABLE=0`).

## Constraints

- Layer boundaries from `AGENTS.md` are hard. The refresh is Renderer-side scene logic, not Domain;
  `IO` only round-trips the fields and must not learn what an anchor means.
- Only the main thread mutates state visible in the project or UI.
- The refresh runs every frame for every arrow - keep it a loop over anchored arrows with no
  allocation, not a rebuild of anything.
- `.cpp` files stay under ~500 lines. Extract rather than append.
- Do not build or run anything needing an approval step. Report what you changed; the dispatching
  session builds, runs the suite and exercises the app.
