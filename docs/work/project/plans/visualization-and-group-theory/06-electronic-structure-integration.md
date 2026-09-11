# 6. Electronic-structure integration

Source: locked plan point 6

## Scope

- Reuse `ElectronicStructureModel`, `ElectronicStructureSession`, `OrbitalRecord`,
  `OrbitalGridData`, occupation-diagram state, and existing WAVECAR loading/cache/rendering.
- Allow a basis component to reference an electronic orbital by calculation/source, spin, and band
  rather than copying its grid data.
- Support mixing procedural, manual, bond, WAVECAR, and occupation-diagram components in one visual
  linear combination.
- Store coefficients and phase in the basis model; allow visually tuned scaling without changing
  computed coefficients.
- Mark combinations whose components lack a validated common basis or normalization.

## Reuse in this repo

`src/Domain/Electronic/ElectronicStructureModel.{hpp,cpp}`,
`src/Presentation/Panels/ElectronicStructure{Panel,Session}.{hpp,cpp}`,
`src/ScientificRuntime/Python/VaspOrbitalGrid{Bridge,Conversion,Job}.{hpp,cpp}`.
