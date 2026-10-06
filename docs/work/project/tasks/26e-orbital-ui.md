# Task 26e: orbital UI - grouped menu, selection, properties, persistence

## Goal

An orbital stops being a thing you can only create and never touch again. After this task the Add
menu lists presets in five drawers instead of one nineteen-item column, an orbital can be clicked in
the viewport and edited in the Object Properties panel like every other scene object, "add on this
atom" is an explicit choice rather than a side effect of what happened to be selected, and orbitals
survive closing and reopening the project.

Task 26d must be merged first - this task consumes `AllOrbitalPresetGroups()` and
`OrbitalPresetsInGroup()`.

## Files to create or change

- `src/Presentation/Panels/RendererPanel.cpp` - the viewport Add submenu. Replace the flat preset
  loop with one `ImGui::BeginMenu` per `AllOrbitalPresetGroups()` entry, listing
  `OrbitalPresetsInGroup(group)`. Under it, when exactly one atom is selected, a separate
  `Na zaznaczonym atomie` entry that anchors the new orbital to that atom
  (`SceneOrbital::anchorAtoms`) instead of dropping it at the click position; when two are
  selected, the same for a two-centre preset. Keep `MakeDefaultSceneOrbital` as the one place a
  default orbital is built - do not grow a second copy of that logic here.
- `src/Presentation/Panels/RendererPanel.cpp` (or the viewport-interaction file it already uses) -
  click selection for orbitals via `PickSceneOrbital`, writing `selectedSceneOrbitals` with the same
  plain-click-replaces / Ctrl-click-adds behaviour `handleSceneArrowInteraction` already has. An
  orbital picked this way participates in the existing transform gizmo the same way a scene arrow
  does: dragging moves `centerA` (and `centerB` with it), and detaches the orbital from its anchor
  atoms if it had any - moving something anchored has to either move the anchor or break it, and
  breaking it is the one that does not silently move an atom.
- `src/Renderer/Scene/SceneOrbitalGeometry.cpp` - implement `SceneOrbitalWorldBounds` and
  `PickSceneOrbital` per the header.
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp` - an "Orbital" section, shown when
  `selectedSceneOrbitals` is non-empty, exposing: preset (a combo grouped the same way as the
  menu), `shell`, `lobeIndex`, `effectiveCharge`, `isoFraction`, `resolution`, `scale`,
  `rotationEuler`, the two lobe colours, `alpha`, `visible`, and centres A/B. Anchoring shows which
  atoms it is attached to with a button to detach. Multi-selection edits every selected orbital,
  matching how the panel already handles several arrows.
- `src/IO/SceneObjectsIO.cpp` - read and write `PersistedSceneOrbital` under
  `kind: SceneOrbital`, keys named exactly as the struct fields.
- `src/Renderer/Scene/SceneObjectPersistence.cpp` - map `PersistedSceneOrbital` to and from
  `RendererWindowState::SceneOrbital`, resolving `anchorAtoms` against the loaded structure the
  same way a pinned measurement's `atomRefs` are resolved. An unknown preset string loads as `p`
  and raises a warning rather than failing the load.

`ObjectPropertiesPanel.cpp` is already 1125 lines. Put the orbital section in its own
`src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp` rather than growing it further, and
re-run `scripts/Windows/GenerateProjects.bat`.

## Files that must NOT be touched

- `src/Renderer/Scene/SceneOrbitalGeometry.hpp`, `src/IO/SceneObjectsIO.hpp`,
  `src/Renderer/RendererWindowState.hpp`, `src/Domain/Electronic/HydrogenicOrbital.hpp` - the
  contract. If you believe a signature is wrong, stop and say so.
- Everything under `tests/`.
- Anything under `src/Domain/` - the physics is task 26d's and is finished.
- `src/Renderer/OpenGl/` - the orbital already renders. This task does not touch the backend.

## Acceptance criteria

1. `tests/Renderer/Scene/SceneOrbitalGeometryTests.cpp` passes in full, including the six new
   `SceneOrbitalBoundsTests`/`PickSceneOrbitalTests` cases.
2. `tests/IO/SceneObjectsIOTests.cpp` passes in full, including `SceneOrbitalRoundTrips` and
   `AnUnanchoredOrbitalKeepsItsOwnCentres`.
3. Every other existing test still passes.
4. The Add > Orbital menu shows five submenus and every preset appears in exactly one of them.
5. Clicking an orbital in the viewport selects it; it appears selected in the Scene Outliner too,
   and the Object Properties panel shows its section.
6. Changing any field in that section re-bakes the mesh (the existing
   `MakeSceneOrbitalMeshKey` cache key already covers every field - if a new field is not in it,
   say so rather than editing the header).
7. Saving a project with orbitals and reopening it restores them, anchors included.

## Constraints

- Layer boundaries in `AGENTS.md` are hard. `Presentation` collects intent and does not reach into
  another layer's state directly; `IO` knows the YAML shape and nothing about `RendererWindowState`.
- `.cpp` files stay under ~500 lines.
- Meshing runs on the main thread, so a properties-panel drag at `resolution` 96 will hitch. That is
  the known ceiling; do not add threading for it in this task.
- Do not create a parallel system next to one that already exists. Orbital selection reuses the
  scene-arrow selection shape; orbital persistence reuses the scene-object file. Search first,
  extend second.
- Do NOT run a build or the tests - the MSBuild toolchain is not reachable from your sandbox.
