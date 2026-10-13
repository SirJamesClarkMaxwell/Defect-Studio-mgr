# Task 112d: Dodaj podsieć + nowa pusta komórka

Implemented by Claude (codex hit its usage limit on 2026-10-14 00:40 after stopping on the parser question; the
`ParseNumericText` extraction was approved in the task file).

- `ModalTransform`: `ParseNumericText(std::string_view)` - the fraction parser, `NumericValue` calls it.
- `CellEditing`: `AddSublattice` (ExpandBasisOverLattice of one seed atom, wrapped offset, appended with sequential
  indices, coincident sites reported) and `EmptyCell`.
- `CellPanel` (`CellPanelSublattice.cpp`): `Dodaj podsieć` - four pattern buttons drawn as small oblique cubes with
  the lattice points (narożniki / + środek / + środki ścian / + środki podstaw), element from the periodic-table
  popup, offset a/b/c fields accepting fractions, live preview (`Doda N atomów`, overlaps with existing atoms,
  recomputed only when inputs or the structure revision change), `Dodaj` through the replace-structure command (one
  undo). `Nowa pusta komórka...`: crystal system + a b c α β γ honouring `GetFieldConstraints`, opens a new window
  via `OpenCrystalStructureAsWindow`. Panel now receives the element and atom-style tables at registration.
- Empty structures: the renderer bootstrap already frames a unit box when there are no atoms
  (`RendererStartupBootstrap.cpp:93`); POSCAR export of an empty cell not checked.
- Tests: `CellEditingTests` (two FCC sublattices in an empty cubic cell = zincblende, overlaps reported and kept,
  wrapped offset), `ModalTransformTests.ParseNumericTextIsTheSessionParserForPlainStrings`. Debug build + these
  tests green. GUI not checked (screen locked at night).
