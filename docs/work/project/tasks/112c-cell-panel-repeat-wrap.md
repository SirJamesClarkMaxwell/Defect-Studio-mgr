# Task 112c: "Komórka" panel - edit the cell in place, repeat cell, wrap atoms (undoable)

Plan: `docs/work/project/plans/2026-10-13-structure-building-gizmo.md` (slice 1). Decision recorded there: changing
the cell keeps **fractional** positions by default.

## Goal

Chalcopyrite walkthrough step 2 and 5 work without leaving the window: repeat the zincblende cell 1x1x2 in place,
then set c/a keeping fractional positions; atoms dragged outside the cell can be wrapped back. Today the cell of an
open structure cannot be edited at all, and `SupercellBuilderPanel` opens the supercell as a NEW window.

## Design

1. **Domain helpers** (pure, sync, `src/Domain/Crystal/`, new `CellEditing.{hpp,cpp}`):
   ```cpp
   enum class CellEditPositions { KeepFractional, KeepCartesian };
   // New cell vectors; atoms (and vacancies, defectFrame origin if present) follow the policy. Fractional
   // coordinates and Cartesian positions stay consistent afterwards. Error for a singular / non-finite cell.
   [[nodiscard]] Result<CrystalStructure> WithCell(const CrystalStructure &structure, const LatticeCell &cell,
       CellEditPositions positions);
   // Same cell shape from lengths/angles (a along x, b in xy - BuildLatticeCell convention), keeping the
   // current orientation of a and the a-b plane (rotate the conventional cell onto the current a, a-b plane).
   [[nodiscard]] LatticeCell CellFromParameters(const LatticeCell &current, const LatticeParameters &parameters);
   [[nodiscard]] LatticeParameters ParametersOf(const LatticeCell &cell);
   // Every atom (and vacancy) moved into [0,1) fractional; reports nothing removed. Coincident sites after
   // wrapping are returned as indices (FindCoincidentAtomIndices), not deleted.
   [[nodiscard]] CrystalStructure WrapIntoCell(const CrystalStructure &structure, std::vector<std::size_t> &outCoincident);
   ```
   Repeat in place reuses `BuildSupercell(structure, SupercellMatrix::Diagonal(n, m, k))` - check what it does with
   vacancies / defectFrame and document; if it drops them, say so in the panel tooltip rather than extending it.
2. **One undoable renderer command** (`src/Renderer/Commands/`, next to the atom edit commands, same registration
   pattern): replaces the window's structure (cell, atoms, vacancies, defectFrame) with a given new structure,
   regenerates bonds, marks the structure modified, rebuilds the snapshot (`RebuildAndSync`-style), clears the
   selection (indices change); Undo restores the previous structure exactly. Three intents use it: cell edit,
   repeat, wrap. One command per user action (a drag of a cell slider commits once, on release).
3. **Panel `Komórka`** (`src/Presentation/Panels/CellPanel.{hpp,cpp}`, category like the structure panels, registered
   where the other structure panels are; visible from the View menu), for the active renderer window:
   - read-out + edit: `a b c` [A] and `α β γ` [deg] as DragFloat in a 2x3 borderless grid with captions above,
     `c/a`, volume [A^3]; edits apply on release (one undo step); angles snap to 60/90/120 while Ctrl is held
     (drag only; typed values untouched);
   - `Pozycje przy zmianie komórki:` radio `ułamkowe` (default) / `kartezjańskie`, tooltip explaining both;
   - `Powtórz komórkę` with three InputInt `n m k` (1..10) + button (tooltip: in place, atoms x n*m*k; how
     vacancies are handled), disabled with a reason for a non-periodic structure;
   - `Zawiń atomy do komórki` button; after it, a line `Nakładające się atomy: N` when coincident sites appear
     (no deletion);
   - no structure / non-periodic window: one disabled line saying why.
   UI rules: labels left/above, every control has a tooltip, disabled controls say why.

## Files to create or change

- new `src/Domain/Crystal/CellEditing.{hpp,cpp}`, new `src/Presentation/Panels/CellPanel.{hpp,cpp}`
- `src/Renderer/Commands/RendererAtomEditCommands.{hpp,cpp}` or a new `RendererCellEditCommand.{hpp,cpp}`,
  `src/Renderer/Commands/RendererCommandRegistration.cpp`
- panel registration (find where structure panels like `SupercellBuilderPanel` are registered - not `EditorLayerAnalysis.cpp`)
- tests: new `tests/Domain/Crystal/CellEditingTests.cpp`, command test next to the atom edit command tests

## Files that must NOT be touched

- `src/Presentation/Panels/Phonon*`, `src/IO/PhononPanelSettingsIO*`, `src/IO/AnalysisDatasetIO*`,
  `src/Presentation/EditorLayerAnalysis.cpp` (another task runs now)
- `src/Renderer/Scene/ModalTransform*` (done in 112a)
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-112c-cell-panel.md`

## Acceptance criteria (GoogleTests)

1. `WithCell` KeepFractional on zincblende (a = 5.43) to c = 2a: fractional coords unchanged, Cartesian z doubled;
   KeepCartesian: Cartesian unchanged, fractional z halved. Singular cell -> error.
2. `CellFromParameters(ParametersOf(cell))` reproduces a hexagonal and a triclinic cell within 1e-4 (including
   orientation); changing only c keeps a and b vectors identical.
3. `WrapIntoCell`: atom at fractional (1.25, -0.5, 0.999999) -> (0.25, 0.5, ~0.999999 or 0); two atoms that land on
   one site are reported, both kept.
4. Repeat 1x1x2 of the 8-atom conventional zincblende -> 16 atoms, c doubled, through the command; undo restores
   8 atoms and the original cell exactly; redo again 16.
5. Report: files changed, tests added, vacancy/defectFrame behaviour of repeat, anything not done.

## Constraints

- Domain stays UI-free; Presentation sends intents through the command registry (undo), never mutates the domain directly.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild (hung nodes lock the build tree); I build and test. Do not commit.
