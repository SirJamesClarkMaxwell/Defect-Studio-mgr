# Task 106e: mode filter "min S_k"; arrow threshold renamed

User 2026-10-13: "próg udziału nie powoduje pokazania mniejszej ilości elementów w tabeli". Chosen
fix: a separate mode filter by Huang-Rhys factor; the arrow slider keeps its meaning.

## Goal
1. `PhononPanelSettings::minHuangRhys` (double, default 0 = no filter), persisted as
   `view.minHuangRhys` in `phonon_panel.yaml` (old files load with 0).
2. `PhononPanelSession::VisibleModes()` also drops modes with `huangRhys < minHuangRhys`. Table,
   β/S_k stems and keyboard navigation all use VisibleModes already - check every caller.
   The selected mode stays selected even when filtered out (its "Wybrany mod" line stays).
3. UI: next to the irrep filter row above the table: caption "min S_k" (label left) + `DragFloat`
   (logarithmic, 0 .. 1, format "%.4g", speed small), tooltip "Pokaż tylko mody o czynniku
   Huanga-Rhysa S_k ≥ progu (udział modu w S)". Right of it: "modów: <shown> z <all non-translation>".
4. Visualisation caption "Próg udziału" -> "Próg strzałek" (tooltip unchanged: per-atom |e_i|^2 of
   the selected mode; add "nie filtruje tabeli - do tego służy min S_k").

## Files to create or change
- `src/IO/PhononPanelSettingsIO.{hpp,cpp}`, `src/Presentation/Panels/PhononPanelSession.{hpp,cpp}`,
  `src/Presentation/Panels/PhononModesPanelView.cpp` (+ whichever panel file draws the filter row)
- tests: `tests/IO/PhononPanelSettingsIOTests.cpp`, `tests/Presentation/Panels/PhononPanelSessionTests.cpp`

## Files that must NOT be touched
- `Vendor/`, `scripts/`, `install/`, `test-directory/`, `premake5.lua`, `src/Renderer/`, other panels

## Acceptance criteria
1. Session test: three modes S_k 0.001 / 0.05 / 0.5, min 0.01 -> two visible; min 0 -> all;
   translations never visible.
2. Settings round-trip with `minHuangRhys`; file without it -> 0.
3. Reviewer builds Debug+Release, tests green except known skips.

## Constraints
- UI conventions (labels left/above, tooltips). PowerShell built-ins only; no Get-Content/Set-Content
  on UTF-8 files. `.cpp` < ~500 lines.
