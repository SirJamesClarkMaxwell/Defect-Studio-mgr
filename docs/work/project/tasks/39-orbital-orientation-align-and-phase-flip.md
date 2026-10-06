# Task 39: align several orbitals' orientation, and flip an orbital's phase

From task 35 item #3: "jak dodałem orbitale to może powinniśmy dodać możliwość jakiejś łatwiejszej
orientacji? aby wskazywały w tą samą stronę? oraz zamiany fazy?" - two asks, both scoped here.

## Part A: align orientation

**Goal.** With two or more orbitals selected (single-centre presets only - two-centre presets take
their orientation from their two atom centres and ignore `rotationEuler` already, see
`SceneOrbitalGeometry.cpp`'s `RotationAppliesToSingleCentrePresetsOnly` test), a button copies the
first selected orbital's `rotationEuler` to every other selected orbital, so they all point the
same way. This is the same shape as the existing multi-selection style broadcast for arrows
(`DrawSelectedSceneArrowProperties` in `SceneArrowEditorWidget.cpp` - representative edited, then
copied to the rest) - follow that pattern, do not invent a new one.

**Where.** The orbital multi-selection editor in `ObjectPropertiesPanelOrbital.cpp` (or wherever
`SceneObjectMultiSelection.hpp` routes a multi-orbital selection to today - check before assuming
the filename). Add an "Align orientation" button next to whatever other shared controls already
exist there.

## Part B: phase flip

**Goal.** A button/toggle on a selected orbital (single or multi-select) flips its phase - the
positive and negative lobes swap everywhere the orbital is evaluated, not just its display colours.

**How.** `OrbitalTerm::coefficient` (`Domain/Electronic/HydrogenicOrbital.hpp:48`) is what a
phase flip must negate - it is already the per-term sign/weight the wavefunction evaluator reads.
Add a field to `RendererWindowState::SceneOrbital` (e.g. `bool phaseFlipped = false;`, beside
`rotationEuler`/`scale`/`stretch` in `RendererWindowState.hpp`) and apply it in
`BuildOrbitalWavefunction` (`Renderer/Scene/SceneOrbitalGeometry.cpp`) - **after** `MakeOrbitalPreset`
builds the terms, negate every term's `coefficient` when `phaseFlipped` is set, rather than
threading a sign through `OrbitalPresetSettings`/`Domain/Electronic/OrbitalPresets.cpp`. The preset
builders stay a pure physical description; a phase flip is a scene-level decoration on top of that,
the same way `stretch`/`scale` already are - keep it at that layer, do not touch `OrbitalPresets.cpp`.
Persist the new field (`IO/SceneObjectsYaml.cpp`/`SceneObjectsIO.hpp`, next to `rotationEuler`) and
hash it into whatever already invalidates the mesh cache when a rendering-relevant field changes
(check `SceneOrbitalGeometry.cpp`'s hashing function, the same shape `SceneArrowGeometryHash`
uses for arrows).

## Files to create or change

- `src/Renderer/RendererWindowState.hpp` - the new `phaseFlipped` field on `SceneOrbital`.
- `src/Renderer/Scene/SceneOrbitalGeometry.cpp` - apply the flip in `BuildOrbitalWavefunction`;
  include it in the mesh cache key hash.
- `src/IO/SceneObjectsIO.hpp`, `src/IO/SceneObjectsYaml.cpp` - persist it, following exactly how
  `rotationEuler` is already read/written there.
- `src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp` (confirm the real filename first) -
  the "Align orientation" button and the phase-flip toggle.
- New tests under `tests/` for anything with logic in it (see Acceptance criteria).

## Files that must NOT be touched

- `src/Domain/Electronic/HydrogenicOrbital.{hpp,cpp}`, `src/Domain/Electronic/OrbitalPresets.{hpp,cpp}` -
  the physical model stays pure; a phase flip is applied one layer up, not inside preset
  construction.
- `src/Renderer/Scene/SceneTransform.{hpp,cpp}` - unrelated to this task; the existing rotation
  gizmo already writes `rotationEuler` correctly (task 38 just fixed its Z-axis case) and needs no
  change here.
- Anything under `src/App/`, `src/ScientificRuntime/`.

## Acceptance criteria

1. A GoogleTest asserts that setting `phaseFlipped = true` on an orbital negates every term's
   `coefficient` in the `OrbitalWavefunction` `BuildOrbitalWavefunction` returns, compared to the
   same orbital with `phaseFlipped = false`.
2. A GoogleTest asserts `EvaluateOrbital` returns the negated value at the same probe point once
   phase-flipped (the positive lobe reads negative and vice versa).
3. A GoogleTest asserts the mesh-cache-key hash differs between a flipped and unflipped orbital
   that are otherwise identical (so toggling it actually triggers a rebuild - this is exactly the
   kind of cache-invalidation gap item #12 turned out NOT to be, for arrows; do not let it become
   one here for orbitals).
4. A GoogleTest round-trips `phaseFlipped` through YAML save/load.
5. A GoogleTest (or a UI-level test if the multi-selection editor already has coverage to extend)
   asserts "Align orientation" copies the representative orbital's `rotationEuler` to the rest of
   the selection, leaving two-centre-preset orbitals in the selection untouched (their orientation
   is not user-editable in the first place).
6. Full Release test suite green: 2 skipped is the `DS_PYTHON_CAPI_AVAILABLE=0` count, not a
   regression.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer/Scene/` has no ImGui in it.
- No exceptions in rendering paths.
- `.cpp` files stay under ~500 lines - split rather than grow.
- UI strings in this codebase are unaccented Polish. Match the surrounding style.
- Do not build or run anything needing an approval step. Report what you changed; the dispatching
  session builds, runs the suite and exercises the app.
