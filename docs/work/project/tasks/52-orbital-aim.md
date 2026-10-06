# Task 52: aim orbitals

Second task of `docs/work/project/plans/vacancy-and-defect-orbitals.md` - read its 52 section.

## Goal

A single-centre scene orbital (p, d, sp/sp2/sp3 lobe) can be pointed at a vacancy, a selected atom
or the 3D cursor with one click in its properties, and the Add > Orbital menu can drop the
dangling-bond sp3 lobes of the selected atoms, each already pointing into the vacancy they
surround - the NV- picture in one action.

## The contract, already written

- `src/Domain/Electronic/HydrogenicOrbital.hpp` - `OrbitalPresetMemberAxis`.
- `src/Renderer/Scene/SceneOrbitalAim.hpp` - **new**; `AimSceneOrbitalEuler`,
  `CollectOrbitalAimTargets`, `ResolveDanglingBondTarget`, `MakeDanglingBondOrbitals`.
  **Read every comment.**
- Tests: `tests/Domain/Electronic/OrbitalPresetMemberAxisTests.cpp`,
  `tests/Renderer/Scene/SceneOrbitalAimTests.cpp`.

## What already exists - reuse it

- `HybridDirection` and the P/D/F `mValues` tables in `OrbitalPresets.cpp` - the member axes are
  those directions; derive from them, do not restate a second table that can drift.
- `RotationFrame` (SceneOrbitalGeometry.cpp) / `RotatedEulerDegrees` (SceneTransform.cpp) - the
  Euler convention. `glm::rotation(from, to)` gives the minimal quaternion and handles antiparallel.
- `MakeDefaultSceneOrbital(windowState, preset, seed, anchorAtoms)`, `IsTwoCenterPreset`.
- `ObjectPropertiesPanelOrbital.cpp` - `DrawUndoableValue` / the existing rotation row: the aim
  must be one undo step like a typed rotation.
- `RendererPanelOrbitalMenu.cpp` - the existing "one orbital per selected atom" creation path,
  including how new orbitals get ids, persist keys and an undo snapshot.

## Files to create or change

- `src/Domain/Electronic/OrbitalPresets.cpp` - `OrbitalPresetMemberAxis`.
- `src/Renderer/Scene/SceneOrbitalAim.cpp` - **new**.
- `src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp` - under the rotation row, for a
  single-centre orbital whose member has an axis: combo "Skieruj na" over
  `CollectOrbitalAimTargets` + button "Skieruj". The centre is the resolved centre
  (`ResolveSceneOrbitalCenters(...).centerA`). Hidden for two-centre presets; disabled with a
  tooltip ("Ten orbital nie ma osi") for s / axis-less members. Allowed for an anchored orbital -
  the anchor fixes the centre, not the orientation.
- `src/Presentation/Panels/RendererPanelOrbitalMenu.cpp` - in the selected-atoms catalogue, an item
  "Wiązania zwisające → wakans (sp³)", enabled when >= 1 atom is selected: target =
  `ResolveDanglingBondTarget`, orbitals = `MakeDanglingBondOrbitals`, added through the same path
  the existing per-atom items use (one undo step for the whole batch). Tooltip names the target
  ("na V_C #1" or "na centroid zaznaczenia").

Regenerate projects after adding files: `DS_TOOLSET=msc-v143` + `scripts\Windows\GenerateProjects.bat`.

## Files that must NOT be touched

- The contract header declarations/comments and the two test files.
- `src/Renderer/Path/**`, anything bevel-related, `Vendor/**`.
- `SceneObjectsIO.*` / persistence: nothing new is persisted - the aim lands in `rotationEuler`,
  which already is.

## Acceptance criteria

1. Release build of `DefectStudio` and `DefectStudioTests` succeeds.
2. `DefectStudioTests --gtest_filter=OrbitalPresetMemberAxisTests.*:SceneOrbitalAimTests.*:OrbitalPresetTests.*:SceneOrbitalGeometryTests.*:HydrogenicOrbitalTests.*`
   passes.
3. Full suite: no new failures (known: 5 `PathStrokeMesherTests` bevel failures, 1 skip).

## Constraints

- Domain stays renderer-free; `SceneOrbitalAim` is Renderer/Scene and pure (no ImGui, no layers).
- No exceptions; `.cpp` files under ~500 lines (`ObjectPropertiesPanelOrbital.cpp` is at ~410 - if
  the aim UI would push it past 500, put it in its own file with a declaration in
  `SceneOrbitalEditorWidget.hpp`).
