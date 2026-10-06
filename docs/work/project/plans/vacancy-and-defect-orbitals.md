# Vacancy model and defect orbitals

Status: 51, 52, 53 implemented 2026-10-02 (Release build + full suite green; Debug and the manual
UI pass still open). One branch each, chained on `task/50-path-bindings`.

## Why

The figure every point-defect paper draws for a vacancy complex (NV-, SiV, V_B in hBN) is three
things at once: the vacancy as a dashed circle, the dangling-bond sp3 lobes of its neighbours
pointing into it, and the symmetry-adapted molecular orbitals (a1, e_x, e_y, ...) those lobes
combine into. Defect Studio has all three ingredients and none of the picture:

- `CrystalStructure::vacancies` (`VacancySite`) is recorded by every atom delete (`ApplyVacancy`),
  and `VacancyRenderStyle` is loaded from the atom-style YAML, but nothing draws a vacancy, nothing
  saves one (a project stores structures as POSCAR, which has no vacancies), and undoing a delete
  leaves its `VacancySite` behind.
- Single-centre scene orbitals (p, sp3 lobes, ...) can be rotated only by typing Euler angles. A
  dangling bond pointed at a vacancy is trial and error.
- The Group Theory panel already projects symmetry-adapted vectors (`PointGroupReduction::
  projectedVectors`) over a site basis built from the selected atoms - the NV- a1/a1/e/e
  coefficients - but only prints them.

## Tasks

### 51 - Vacancy markers (`task/51-vacancy-markers`)

Make the existing domain vacancy visible, editable and durable.

- **Render.** `RendererStructureData::vacancies` is filled by `BuildRendererStructureData` from
  `structure.vacancies` and the `VacancyRenderStyle`. Each vacancy is a camera-facing disc with a
  dashed ring (`BuildVacancyMarkerMesh`, Renderer/Scene, pure and tested), drawn through the
  isosurface overlay pipeline exactly as `ScenePlane` is - no new shader. Modes reuse
  `VacancyRenderMode`: Ghost = translucent fill + dashed ring, Wireframe = ring only, Solid = opaque
  fill + ring. Style gains `dashCount` and `ringWidth`, persisted in the atom-style YAML.
- **Edit.** One undoable command replaces a structure's vacancy list (add / remove are both "set
  the list"). Add at the selection centroid - which is where the site of a relaxed vacancy is
  (select its 3-4 neighbours) - or at the 3D cursor. Remove from a new "Vacancies" outliner
  group, whose eye toggles `RendererWindowState::showVacancies`.
- **Fix.** Undoing an atom delete restores `structure.vacancies` as well as atoms and bonds.
- **Persist.** `scene_objects.yaml` gains an optional per-structure `vacancies:` list beside
  `objects:`; absent = none, so `formatVersion` does not change.
- **Style editor.** The Element Catalog gets a vacancy row (colour, radius, opacity, mode, dashes,
  ring width), propagated like an element style edit.

### 52 - Aim orbitals (`task/52-orbital-aim`)

- **Domain.** `OrbitalPresetMemberAxis(preset, lobeIndex)`: the unit direction, in the orbital's own
  frame, of the member's main positive lobe - p_x/p_y/p_z, each sp/sp2/sp3 lobe, the d members
  that have one; nullopt for s, for f members other than f_z3, and for the two-centre presets.
- **Renderer/Scene.** `AimSceneOrbitalEuler`: the `rotationEuler` that turns that axis onto
  `target - centre`, composed as the minimal rotation onto the current orientation, so the twist
  about the lobe axis the user already had is kept.
- **UI.** Orbital properties: "Skieruj na" with targets = each vacancy, each selected atom (the
  atom that is about to be removed is still an atom), the 3D cursor. Orbital add menu: "Wiązania
  zwisające -> wakans": one sp3 lobe per selected atom, each aimed at the chosen vacancy (or the
  selection centroid when there is none) - the NV- dangling-bond picture in one click.
- One-shot, not live: the aim is written into `rotationEuler` once. ponytail: a persisted live aim
  target (re-resolved every frame like `anchorAtoms`) is the upgrade if relaxation playback needs
  lobes that keep pointing.

### 53 - Group-theory orbitals in the scene (`task/53-salc-orbitals`)

- **Model.** `SceneOrbital` gains an optional LCAO component list; when non-empty it replaces the
  preset. Each component = a single-centre preset member on an anchored atom, with its own
  orientation and a coefficient. `BuildOrbitalWavefunction` concatenates the components' terms
  scaled by their coefficients - `OrbitalWavefunction` is already a term list, so the evaluator,
  sampler, mesher, cache and renderer do not change. Persisted with `PersistedAtomRef`s.
- **Builder.** `BuildSalcSceneOrbital(vector, siteAtoms, centre, basis)`: one component per basis
  site, the basis function aimed at the analysis centre with task 52's maths, coefficient =
  `ExactCoefficient::numeric`. Basis: sp3 dangling bond (default), p toward the centre, s. A vector
  with any non-zero imaginary coefficient is refused (complex irreps of C3, C4h ...) - plot the
  real combination of the pair instead; ponytail until someone needs it.
- **UI.** Group Theory panel: "Rysuj" per projected vector (each row of a degenerate irrep is its
  own orbital: e_x and e_y), a basis combo, and "Wakans #i" as an analysis-centre option.
- **Out of scope.** Many-electron multiplet states (`MultipletWavefunction`) are sums of Slater
  determinants, not one 3D field; there is no isosurface to draw. A determinant's density or spin
  density is a separate feature.

## Order and dependencies

51 -> 52 (vacancies are aim targets) -> 53 (uses 52's aim maths and 51's vacancies as the centre).
Each task: contract headers + failing tests here, `.cpp` by Codex, review, Release build, full
suite. Debug + Release before any merge to main.
