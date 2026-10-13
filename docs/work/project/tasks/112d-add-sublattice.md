# Task 112d: "Dodaj podsieć" and an empty starting cell in the Komórka panel

Plan: `docs/work/project/plans/2026-10-13-structure-building-gizmo.md` (slice 1, last part). Builds on 112c
(`CellPanel`, `CellEditing`, the undoable replace-structure command `kReplaceStructureCommandId`).

## Goal

Chalcopyrite walkthrough step 1 without knowing symmetry: start from an empty cubic cell, add "cations on the
corners + face centres" (FCC pattern), add "S on the same pattern shifted by (1/4, 1/4, 1/4)". The user picks a
visual pattern, a species and an offset; never a space group or Wyckoff letter.

## Design (binding)

1. **Domain** (`CellEditing.{hpp,cpp}`):
   ```cpp
   // Atoms of `species` at every lattice point of `centering` (GetCenteringTranslations) shifted by `offset`
   // (fractional, wrapped into [0,1)), appended to a copy of the structure. outCoincident: indices (in the result)
   // of atoms sharing a site with another atom - reported, never deleted (FindCoincidentAtomIndices).
   [[nodiscard]] CrystalStructure AddSublattice(const CrystalStructure &structure, const std::string &species,
       BravaisCenteringPreset centering, const glm::vec3 &offset, std::vector<std::size_t> &outCoincident);
   // Periodic structure with this cell and no atoms (name "Nowa komórka").
   [[nodiscard]] CrystalStructure EmptyCell(const LatticeCell &cell);
   ```
   Reuse `ExpandBasisOverLattice` / `GetCenteringTranslations`; no new expansion code. Labels follow the existing
   convention for new atoms (search how `ApplyInterstitial` / the wizard name atoms).
2. **Panel `Komórka`** gets a section `Dodaj podsieć`:
   - `Wzór` as four small visual buttons with a drawn 2D glyph of the cell (square with dots: corners only `P`,
     + centre `I`, + face centres `F`, + base centres `C`) and the caption `narożniki`, `+ środek`, `+ środki ścian`,
     `+ środki podstaw`; tooltip says how many atoms per cell (1/2/4/2);
   - `Pierwiastek` text field with the existing element validation (search the wizard's species input) + colour dot;
   - `Przesunięcie` three fields a/b/c that accept fractions (`1/4`; reuse `NumericValue`-style parsing from
     `ModalTransform` if it can be called on a string - otherwise a tiny shared parser, not a copy), default 0;
   - preview line: `Doda N atomów (Fe: 4)`, `Nakładające się z istniejącymi: K` (computed live, nothing applied);
   - `Dodaj` applies through the replace-structure command (one undo step).
   - `Nowa pusta komórka…`: popup with crystal system (reuse `CrystalSystem` + `GetFieldConstraints` +
     `BuildLatticeCell`) and a, b, c, α, β, γ fields respecting the locks, `Utwórz` opens a NEW window with
     `EmptyCell` (reuse the path the supercell builder uses: `OpenCrystalStructureAsWindow`; check an empty atom list
     is accepted end to end - renderer, cell box, saving as POSCAR; if POSCAR export refuses zero atoms, say so in
     the tooltip and the report, do not change the exporter).

## Files to create or change

- `src/Domain/Crystal/CellEditing.{hpp,cpp}`, `src/Presentation/Panels/CellPanel.{hpp,cpp}` (split if > 500 lines,
  e.g. `CellPanelSublattice.cpp`)
- tests: `tests/Domain/Crystal/CellEditingTests.cpp`

## Files that must NOT be touched

- Phonon files, `src/IO/*`, `EditorLayerAnalysis.cpp` (other tasks); `ModalTransform*`
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-112d-add-sublattice.md`

## Acceptance criteria (GoogleTests)

1. Empty cubic cell a = 5.43; `AddSublattice("Zn", F, 0)` -> 4 atoms at the FCC positions; then
   `AddSublattice("S", F, (1/4,1/4,1/4))` -> 8 atoms = zincblende (compare fractional sets within 1e-5).
2. `AddSublattice("Zn", P, (1,0,0))` on the 8-atom zincblende reports the coincident pair, keeps both atoms.
3. Offset (1.25, -0.25, 0) wraps to (0.25, 0.75, 0).
4. Report: files, tests, empty-structure end-to-end findings, anything not done.

## Constraints

- Domain UI-free; edits through the command (undo). UI conventions: labels left/above, tooltips everywhere,
  disabled controls say why.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
