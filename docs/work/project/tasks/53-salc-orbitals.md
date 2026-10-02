# Task 53: group-theory orbitals in the scene

Third task of `docs/work/project/plans/vacancy-and-defect-orbitals.md` - read its 53 section.

## Goal

Each symmetry-adapted vector the Group Theory panel projects (for NV-: a1, a1, e_x, e_y over the N
and three C dangling bonds) gets a "Rysuj" button that drops the corresponding one-electron
orbital into the viewport: an LCAO of one basis function per site (sp3 dangling bond by default,
p or s on request), each aimed at the analysis centre, weighted by the projection coefficients.
The orbital is a normal scene orbital - iso, colours, phase flip, scale, outliner, save/reopen.
A vacancy can be chosen as the analysis centre.

## The contract, already written

- `src/Renderer/RendererWindowState.hpp` - `SceneOrbital::LcaoComponent`, `lcaoComponents`,
  `displayName`. **Read every comment.**
- `src/Renderer/Scene/SceneOrbitalGeometry.hpp` - the LCAO rule on `BuildOrbitalWavefunction`.
- `src/Renderer/Scene/SceneOrbitalLcao.hpp` - **new**; `SalcBasisFunction`, `BuildSalcSceneOrbital`,
  its error codes.
- `src/IO/SceneObjectsIO.hpp` - `PersistedOrbitalLcaoComponent`, the two new
  `PersistedSceneOrbital` fields.
- Tests: `tests/Renderer/Scene/SceneOrbitalLcaoTests.cpp`,
  `tests/Renderer/SceneOrbitalLcaoPersistenceTests.cpp`.

## What already exists - reuse it

- `MakeOrbitalPreset`, `RotationFrame`, `ResolveAnchor` (SceneOrbitalGeometry.cpp) - a component is
  exactly a single-centre orbital; build it the way a single-centre SceneOrbital is built.
- `AimSceneOrbitalEuler` (task 52, `SceneOrbitalAim.hpp`) - the aim maths.
- `ValenceShell` / `ValenceEffectiveCharge`, `MakeDefaultSceneOrbital`.
- `SceneObjectPersistence.cpp` - how `anchorAtoms` are written as `PersistedAtomRef`s and rebound on
  load; components use the same helpers.
- `SceneObjectsYaml.cpp` - how `anchorAtoms` lists are emitted/parsed; keys `lcaoComponents`
  (list of maps: `atom`, `preset`, `shell`, `lobeIndex`, `effectiveCharge`, `rotationEuler`,
  `coefficient`) and `displayName`.
- `GroupTheoryPanel` - `m_Basis` (a `SelectionBasis`: sites + atomIndices + centre), `m_BasisKey`,
  `currentBasisKey()`, `drawProjectedVectors` (GroupTheoryPanelTables.cpp), `m_PhysicalBuffers`
  (user-typed physical labels per vector).
- The orbital add path in `RendererPanelOrbitalMenu.cpp` / `SceneOrbitalOperations.cpp` - how a new
  SceneOrbital gets its id, registry entity, persist key and undo snapshot. Use it.

## Files to create or change

- `src/Renderer/Scene/SceneOrbitalLcao.cpp` - **new**.
- `src/Renderer/Scene/SceneOrbitalGeometry.cpp` - the LCAO branch in `BuildOrbitalWavefunction`,
  `ResolveSceneOrbitalCenters`, `MakeSceneOrbitalMeshKey`, `ResolveAnchoredOrbitals`;
  `SceneOrbitalWorldBounds` / picking must work for an LCAO orbital (they go through the
  wavefunction / centres - check).
- `src/Renderer/Scene/SceneObjectPersistence.cpp`, `src/IO/SceneObjectsYaml.cpp` (or wherever
  `PersistedSceneOrbital` is emitted/parsed) - the new fields.
- Gizmo / modal transform for orbitals (`SceneTransform*.cpp`): an orbital with lcaoComponents takes
  Scale only; Translate and Rotate leave it unchanged.
- `src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp` - for an LCAO orbital: title =
  displayName, a read-only table "Składowe" (atom `<element> #<index+1>`, function = preset member
  display name, coefficient `%.4f`), and hide preset / shell / lobe / effective charge / centres /
  anchoring / rotation / aim rows. Iso, resolution, colours, alpha, phase flip, scale, stretch stay.
- Scene Outliner orbital row: `displayName` when non-empty.
- `src/Presentation/Panels/GroupTheoryPanel.{hpp,cpp}` + `GroupTheoryPanelTables.cpp`:
  - `CentreMode::Vacancy` with a vacancy index: a combo entry per vacancy of the source window's
    structure (`V_C #1`, ...), disabled when there is none; the centre is that vacancy's position.
  - Above the projected-vectors table: combo "Funkcja bazowa" = sp³ (wiązanie zwisające) / p → centrum
    / s.
  - Per projected vector row: button "Rysuj" - `BuildSalcSceneOrbital(window, vector, *m_Basis,
    function, name)` on the window `m_BasisKey->windowId`, added through the orbital add path
    (one undo step). name = the row's physical label if the user typed one, else
    `<irrep> #<occurrence+1>`, plus ` (<row+1>)` when the irrep's dimension is > 1. Disabled with a
    tooltip when `currentBasisKey() != m_BasisKey` ("Zaznaczenie się zmieniło - przelicz") or the
    window is gone. A `BuildSalcSceneOrbital` error is shown the way the panel shows `m_Error`.

Regenerate projects after adding files: `DS_TOOLSET=msc-v143` + `scripts\Windows\GenerateProjects.bat`.

## Files that must NOT be touched

- The contract declarations/comments listed above and the two test files.
- The Python bridge and scripts (`GroupTheoryBridge*`, `AnalyzePointGroupJob*`, `scripts/python/**`):
  the coefficients are already in the result.
- `src/Renderer/Path/**`, anything bevel-related, `Vendor/**`.

## Acceptance criteria

1. Release build of `DefectStudio` and `DefectStudioTests` succeeds.
2. `DefectStudioTests --gtest_filter=SceneOrbitalLcaoTests.*:SceneOrbitalLcaoPersistenceTests.*:SceneOrbital*:SceneObjectPersistenceTests.*:SceneObjectsIOTests.*:SceneTransform*`
   passes.
3. Full suite: no new failures (known: 5 `PathStrokeMesherTests` bevel failures, 1 skip).

## Constraints

- Renderer reads domain types (`SymmetryAdaptedVector`, `SelectionBasis`) but does not call the
  bridge; Presentation owns the panel and calls Renderer.
- Many-electron `MultipletWavefunction`s are out of scope - no "Rysuj" on multiplet rows.
- No exceptions; `.cpp` files under ~500 lines (`GroupTheoryPanelTables.cpp` is at ~500 already -
  put the new rows' drawing in a new file if needed).
