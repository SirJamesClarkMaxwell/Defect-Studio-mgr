# Task 96: defect energetics - domain arithmetic

## Goal
Pure C++ arithmetic for the energetics module: formation energy lines, the exact lower envelope
and thermodynamic transition levels, and chemical-potential limits (formation enthalpies, vertex
enumeration of the allowed Delta mu region, validation of hand-typed sets). No files, no UI.
Plan: `docs/work/project/plans/2026-10-11-energetics-auto-and-mu.md`.

## Files to create or change
- `src/Domain/Defects/DefectEnergetics.cpp` (new) - implement every function declared in
  `src/Domain/Defects/DefectEnergetics.hpp`.

## Files that must NOT be touched
- `src/Domain/Defects/DefectEnergetics.hpp` (the contract; stop and say so if a signature is wrong)
- `tests/Domain/Defects/DefectEnergeticsTests.cpp` (the expectations)
- everything outside `src/Domain/Defects/DefectEnergetics.cpp`

## Acceptance criteria
1. `DefectStudioTests.exe --gtest_filter=DefectEnergeticsTests.*` passes (Release x64).
2. The full test suite still passes.
3. `DefectEnergetics.cpp` includes nothing from Presentation, Renderer, IO, App, ScientificRuntime.

## Constraints
- Domain layer: std + glm only. No exceptions thrown for bad input - return nullopt / error string.
- Vertex enumeration: free dimensions = species count - 1; try every combination of that many
  bounds (elemental Delta mu_i <= 0 except a one-element host's own element, and each competing
  phase), solve the small linear system together with the host equality (Gaussian elimination with
  partial pivoting, singular -> skip), keep solutions violating no bound by more than 1e-9 eV,
  deduplicate at 1e-9, list tight bounds (1e-9) in the documented order, sort vertices by
  `limitingPhases`. More than 6 species -> error. Empty result -> error ("no allowed region").
- LowerEnvelope: exact; walk from E_F = 0 choosing the lowest line (ties: smaller slope wins
  going right, then lower index), jump to the nearest crossing with a line of smaller slope.
  Drop zero-length segments (|to - from| < 1e-12). Merge consecutive segments of the same line.
- `.cpp` under ~500 lines.
