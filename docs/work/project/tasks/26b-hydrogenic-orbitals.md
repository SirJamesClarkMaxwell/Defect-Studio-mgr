# Task 26b: hydrogenic orbitals

## Goal

Real solutions of the hydrogen-atom Schrodinger equation, evaluated analytically and sampled onto
the `OrbitalGridData` the renderer already understands - so every shape task 26 lists (`s`, `p`,
`d`, `sp`, `sp2`, `sp3`, and the `sigma` / `pi` / `delta` molecular orbitals with their starred
antibonding partners) comes out of one evaluator instead of a pile of hand-modelled lobes. This is
the half of task 26 that produces figures worth putting in a paper; the stick-between-atoms half is
task 26a and is independent of this one.

Nothing new is rendered by this task. The point is that `SampleOrbitalToGrid` returns the same
struct `VaspOrbitalGridBridge` already returns from a WAVECAR, so the existing GPU
marching-tetrahedra path (`RendererLayer::RegenerateOrbitalIsosurface`, `isosurface_march.comp`)
draws an analytic orbital with its two phases coloured, with no new render path. Wiring an orbital
up as a scene object with its own transform, persistence and properties panel is the task after
this one.

## Files to create or change

- `src/Domain/Electronic/HydrogenicOrbital.cpp` - replace the stub with the real radial functions,
  real spherical harmonics, LCAO evaluation, box sizing and grid sampling.
- `src/Domain/Electronic/OrbitalPresets.cpp` - replace the stub with the preset term-list builders
  and the preset name table.
- `src/Renderer/Scene/IsosurfaceMesher.cpp` - add `grid.origin` to the world position it computes
  for each vertex, so a grid whose box is not anchored at the scene origin meshes in the right
  place. One line; `origin` defaults to zero so every WAVECAR grid is unaffected.
- `src/Renderer/OpenGl/shaders/isosurface_march.comp` (whatever its actual path is - find it) - the
  same `origin` offset, so the GPU path and the CPU reference path keep agreeing. If the shader
  takes the cell as a uniform, `origin` needs to travel with it; find the upload site in
  `OpenGlRendererBackend` and extend it.

## Files that must NOT be touched

- `src/Domain/Electronic/HydrogenicOrbital.hpp` - the contract. If a signature is wrong, say so;
  do not change it.
- Everything under `tests/` - also the contract, for the same reason.
- `src/Domain/Electronic/ElectronicStructureModel.hpp` - the `origin` field is already added.
- Anything under `src/Presentation/`, `src/IO/` or `src/App/` - this task is domain maths plus the
  one-line origin fix in the mesher. Scene objects, persistence and UI are the next task.
- `src/ScientificRuntime/Python/VaspOrbitalGrid*` - the WAVECAR path is unrelated and already
  leaves `origin` at its default.

## Acceptance criteria

1. `tests/Domain/Electronic/HydrogenicOrbitalTests.cpp` passes in full. It checks the radial
   functions against their closed forms (1s decay, the 2s node at two Bohr radii, the two 3s
   nodes), the numerical normalisation of both the radial and the angular factors, the
   orthonormality of the real spherical harmonics, and that sampling reproduces the analytic
   value at the grid points.
2. `tests/Domain/Electronic/OrbitalPresetTests.cpp` passes in full. It pins the lobe conventions
   (sp at 180 degrees, sp2 at 120, sp3 at the tetrahedral 109.47, all with lobe 0 on +z) and the
   physics of the molecular orbitals: a bonding combination has no node between the nuclei, every
   starred one has exactly that node, `pi` vanishes along the whole bond axis and `delta` has two
   nodal planes containing it.
3. `tests/Renderer/Scene/IsosurfaceMesherTests.cpp` still passes, and a grid with a nonzero
   `origin` meshes offset by exactly that much. Add the one test for the offset; the existing
   cases must not change.
4. Every other existing test still passes.

## Constraints

- `Domain` depends on nothing above it - no renderer, no UI, no `App`. `HydrogenicOrbital.cpp` and
  `OrbitalPresets.cpp` include glm and the standard library and nothing else from this repo beyond
  `ElectronicStructureModel.hpp`.
- No exceptions on a rendering path (`AGENTS.md`). Invalid quantum numbers return zero, they do not
  throw; the tests check this.
- `.cpp` files stay under ~500 lines. That is why the presets live in their own file.
- All lengths are Angstrom. `kBohrRadiusAngstrom` in the header is the only conversion.
- Do not add a dependency for special functions. The associated Laguerre polynomials and the real
  spherical harmonics needed here go up to n = 4 / l = 2; write them from their recurrences or
  their explicit low-order forms rather than pulling in a library.
- Do NOT build. The MSBuild toolchain is not reachable from the Codex sandbox; the build and the
  test run happen outside that session.
