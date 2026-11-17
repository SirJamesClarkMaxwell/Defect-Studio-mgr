# Task 113f1: read numeric text tables, experimental spectrum on the eV axis

Plan: `docs/work/project/plans/2026-11-17-phonon-server-results-and-comparison.md` (113f, first half: no UI).
User 2026-11-17: experimental PL from a text file, energy in eV for now, built so more formats can be added.

## Goal

A pure reader for numeric text tables (whitespace/tab/`;`/`,`, headers, comments, decimal comma, Origin 3-line
header) and a domain type for a measured spectrum on the eV axis with a single place to add more axis units.
The UI and plots come in 113f2 and are NOT part of this task.

## Contract (written, binding)

- `src/IO/DataTableIO.hpp`: `ParsedDataTable`, `ParseDataTable`, `LoadDataTable` - implement in
  `src/IO/DataTableIO.cpp` next to the existing writer (same file; no new reader class elsewhere).
- `src/Domain/Vibrations/ExperimentalSpectrum.hpp`: implement in new `ExperimentalSpectrum.cpp`.
- Tests: `tests/IO/DataTableIOTests.cpp` (4 new tests at the end), new `tests/Domain/Vibrations/ExperimentalSpectrumTests.cpp`.

## Files to create or change

- `src/IO/DataTableIO.cpp`, new `src/Domain/Vibrations/ExperimentalSpectrum.cpp`
- `docs/work/project/reports/task-113f1-text-table.md` (report: files, tests, deviations)

## Files that must NOT be touched

- the two headers above and the test expectations
- everything under `src/Presentation`, `src/ScientificRuntime`, `scripts/` - another task (113a) is editing those
  right now in the same working tree; do not touch, format or "fix" them even if they do not compile yet
- `Vendor/`, `install/`, `build/`, other `docs/`

## Acceptance criteria

1. The new tests pass and the existing `DataTableIOTests` still pass (I build and run them).
2. Number parsing is locale-independent (`std::from_chars`, like the writer's formatting test expects).
3. Report written.

## Constraints

- IO has no UI includes; Domain has no IO includes.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
- If a binding detail conflicts with existing code, keep the documented behaviour, note the deviation in the
  report and continue.
