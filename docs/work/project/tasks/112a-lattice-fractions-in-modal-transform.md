# Task 112a: lattice fractions in the modal G/R/S transform

Plan: `docs/work/project/plans/2026-10-13-structure-building-gizmo.md` (slice 1, first part).
User 2026-10-13: build complex cells (e.g. chalcopyrite) with the gizmo and `g -> x/y/z`, `r -> x/y/z` plus
snapping to fractions of lattice vectors, instead of space groups / Wyckoff positions.

## Goal

With orientation **Lattice** (`TransformOrientation::Lattice`), a move is measured in fractions of the cell
vectors, not in Angstrom:
- `G X 1/8 Enter` moves by exactly a/8 (the vector a times 0.125, NOT 0.125 A along the unit a direction);
- Ctrl-drag snaps each lattice coordinate (non-normalized basis) to multiples of 1/8; Ctrl+Shift to 1/24
  (the 1/24 grid contains 1/2, 1/3, 1/4, 1/6, 1/8, 1/12);
- the header shows the fraction and the Angstrom length, e.g. `Move  a (Lattice)  +1/8 a = 0.679 A  Snap 1/8`;
- `U` during the operation toggles lattice units <-> Angstrom (Angstrom = today's behaviour exactly);
- typed fractions work everywhere a number is typed (`1/8`, `-3/4`, `0.5/2`), also for rotate (degrees) and
  scale (factor) - there they are just numbers.
Global/Local/Defect orientations behave exactly as today.

## Contract (add to `src/Renderer/Scene/ModalTransform.hpp`; keep every existing declaration)

```cpp
struct TransformSnapSteps {
    float translate = 0.1f; float rotateDegrees = 5.0f; float scale = 0.1f;
    float latticeFraction = 0.125f; // Increment step in lattice units; Fine = latticeFraction / 3
};
struct ModalTransformSession {
    ... // existing fields
    // Lattice orientation only: true = translations are fractions of the (non-normalized) cell vectors.
    bool latticeUnits = true;
};
// True when the session's translation frame is the lattice and latticeUnits is on (constraint.space ==
// Lattice, or constraint None with orientation Lattice) and bases.lattice is present and non-singular.
[[nodiscard]] bool UsesLatticeUnits(const ModalTransformSession &session);
// '/' appends once, only right after a digit and never as the first char (AppendNumericChar extension).
// NumericValue accepts "[-]num" and "[-]num/num"; denominator 0 or a trailing '/' -> nullopt.
// "a/b" like "1/3" for display: nearest p/q with q <= 24 within 1e-4, else nullopt.
[[nodiscard]] std::optional<std::pair<int, int>> NearestSimpleFraction(float value);
```

Behaviour changes inside existing functions (document them in the header comments):
- `EvaluateModalTransform`, numeric Translate: under `UsesLatticeUnits` the direction is the raw lattice column
  (Axis: that column; None: column 0; Plane: first in-plane column), times the value.
- `EvaluateModalTransform`, mouse Translate snapping: under `UsesLatticeUnits` snap the coordinates in the raw
  (non-normalized) lattice basis with step `latticeFraction` (Increment) or `latticeFraction / 3` (Fine), keeping
  today's per-axis allowed/zeroed rule. Mouse path without snapping is unchanged.
- `ResolveSnapStep` keeps its contract for the Angstrom path; add what you need for the lattice step without
  changing its existing results.
- `FormatModalTransformHeader`: under `UsesLatticeUnits` show the lattice displacement per constrained axis as a
  fraction when `NearestSimpleFraction` finds one (`+1/8 a`), else 4 decimals (`+0.1370 a`), then `= <length> A`
  of the whole translation, and `Snap 1/8` / `Snap 1/24` (fraction text) when snapping. Typed text is shown as
  typed (`1/8`). Angstrom path: unchanged text.

Viewport glue (`src/Presentation/Panels/ViewportModalTransform.cpp`): `/` and keypad divide feed
`AppendNumericChar(session, '/')`; `U` toggles `session.latticeUnits` (only meaningful under Lattice; harmless
otherwise); the hint line under the header (add one if none exists, small and dim) lists
`X/Y/Z oś · Shift płaszczyzna · U jednostki (Sieć) · Ctrl przyciąganie · Enter · Esc`.
Check that `U` is not already bound inside the modal context (search keymap/commands); if it is, pick a free key
and say which.

## Files to create or change

- `src/Renderer/Scene/ModalTransform.{hpp,cpp}`
- `src/Presentation/Panels/ViewportModalTransform.cpp`
- `tests/Renderer/ModalTransformTests.cpp` (add tests; do not change existing expectations)

## Files that must NOT be touched

- Anything under `src/Presentation/Panels/Phonon*`, `src/Presentation/EditorLayer*`, `src/Presentation/PanelDockRegions*`
  (another task edits them right now)
- `src/IO/*`, `src/Domain/*`, `src/App/*` (no config key in this task: the 1/8 default is a struct default)
- `Vendor/`, `install/`, `build/`; `docs/` except a report `docs/work/project/reports/task-112a-lattice-fractions.md`

## Acceptance criteria

1. New tests in `ModalTransformTests.cpp` pass, existing ones unchanged:
   - `NumericValue`: "1/8" = 0.125, "-3/4" = -0.75, "0.5/2" = 0.25; "1/0", "1/", "/" -> nullopt;
     `AppendNumericChar` refuses a leading '/', a second '/', '/' after '-' alone.
   - numeric `G` along Lattice a with a = (5.43, 0, 0): "1/8" -> translation (0.67875, 0, 0);
     along b of `MakeHexLattice()` -> exactly 0.125 * b (not normalized).
   - mouse translate on the hex lattice with Increment snap: inverse(raw lattice) * translation has components
     that are multiples of 1/8 (within 1e-5); Fine -> multiples of 1/24.
   - `latticeUnits = false` reproduces today's results for the same inputs.
   - header under Lattice contains "1/8 a" and "A" (Angstrom sign as the code already prints it); `NearestSimpleFraction`
     (0.3333333) = {1,3}, (0.137) = nullopt, (-0.75) = {-3,4}.
2. No ImGui include in `ModalTransform.*` (pure math stays testable).
3. Report: files changed, tests added, the key chosen for units, anything not done.

## Constraints

- Layers per `AGENTS.md`/`CLAUDE.md`; skewed cells: always the full cell matrix and its inverse, reject singular.
- Use PowerShell built-ins; Git Bash msys tools crash in this sandbox. Do not commit. If MSBuild cannot write
  `build/` (MSB6003), say so - I build and test. No new files, so no project regeneration needed.
