# Task 26d: f orbitals and preset groups

## Goal

`RealSphericalHarmonic` gains the seven real f harmonics (l = 3), `OrbitalPreset` gains `F`, and
the presets stop being a flat list of nineteen by being filed into five groups. After this task the
Add menu can be generated from `AllOrbitalPresetGroups()` instead of hand-listing enumerators, and a
preset added later cannot silently become undrawable.

## Files to create or change

- `src/Domain/Electronic/HydrogenicOrbital.cpp` - extend the `RealSphericalHarmonic` switch to
  l = 3. The closed forms, normalised so the integral of Y^2 over the unit sphere is 1, on a unit
  vector (x, y, z):

  | m | form |
  |---|---|
  | 0 | `sqrt(7/(16 pi)) * (5 z^3 - 3 z)` |
  | +1 | `sqrt(21/(32 pi)) * x * (5 z^2 - 1)` |
  | -1 | `sqrt(21/(32 pi)) * y * (5 z^2 - 1)` |
  | +2 | `sqrt(105/(16 pi)) * z * (x^2 - y^2)` |
  | -2 | `sqrt(105/(4 pi)) * x * y * z` |
  | +3 | `sqrt(35/(32 pi)) * x * (x^2 - 3 y^2)` |
  | -3 | `sqrt(35/(32 pi)) * y * (3 x^2 - y^2)` |

  Keep the existing early-out shape; only the ceiling moves from l > 2 to l > 3.
- `src/Domain/Electronic/OrbitalPresets.cpp` - handle `OrbitalPreset::F` in `MakeOrbitalPreset`
  (one term, l = 3, `shell` clamped up to at least 4 because there is no 3f, `lobeIndex` selecting
  m in the order documented on `OrbitalPresetSettings`: 0, +1, -1, +2, -2, +3, -3), add `"f"` to the
  name/parse table, and implement the four grouping functions.

## Filing

| Group | Presets, in this order |
|---|---|
| `Atomic` | S, P, D, F |
| `Hybrid` | Sp, Sp2, Sp3 |
| `Bonding` | Sigma, Pi, Delta |
| `Antibonding` | SigmaStar, PiStar, DeltaStar |
| `HybridBonding` | SpSigma, SpSigmaStar, Sp2Sigma, Sp2SigmaStar, Sp3Sigma, Sp3SigmaStar |

Group display names are Polish, matching the rest of the UI: "Atomowe", "Hybrydy", "Wiazace",
"Antywiazace", "Wiazania z hybryd". ASCII only - this codebase does not put diacritics in string
literals.

## Files that must NOT be touched

- `src/Domain/Electronic/HydrogenicOrbital.hpp` - the contract. It already declares everything
  this task implements. If you believe a signature is wrong, stop and say so.
- Everything under `tests/`.
- Anything under `src/Renderer/`, `src/Presentation/`, `src/IO/` or `src/App/`. The UI that
  consumes these groups is a separate task; this one is Domain and nothing else.

## Acceptance criteria

1. Every test in `tests/Domain/Electronic/OrbitalPresetTests.cpp` passes - the eight new ones
   (`TheSevenFHarmonicsAreOrthonormal`, `FzCubedHasItsTwoConesAsWellAsItsTwoLobes`, `StopsAboveF`,
   `TheFPresetBuildsAnFTermAndClampsTheShellUpToFour`, `TheSevenFLobesAreSevenDifferentOrbitals`,
   `TheFPresetPersistsAsF`, and the three `OrbitalPresetGroupTests`) plus every pre-existing one.
2. Every other existing test still passes. In particular `EveryPresetFromTheMenuProducesAVisibleMesh`
   in `tests/Renderer/Scene/SceneOrbitalGeometryTests.cpp` is unchanged and must stay green -
   `SuggestOrbitalExtent` has to cope with an f orbital's larger extent without special-casing it.
3. `OrbitalPresetsInGroup` concatenated over `AllOrbitalPresetGroups()` yields all nineteen presets,
   each exactly once.

## Constraints

- `Domain` does not depend on UI, Renderer or App (`AGENTS.md`). No new includes from those layers.
- `.cpp` files stay under ~500 lines. `OrbitalPresets.cpp` is at 258; if the grouping table pushes
  it over, split the grouping into `src/Domain/Electronic/OrbitalPresetGroups.cpp` and re-run
  `scripts/Windows/GenerateProjects.bat` (premake globs sources at generation time).
- No new files unless the line limit forces one. The grouping is a table, not a system.
- Do NOT run a build or the tests - the MSBuild toolchain is not reachable from your sandbox.
