# Task 87: FNV correction panel

Branch `task/87-fnv-panel` off `task/86-fnv-engine`. Engine: `src/Domain/Defects/FnvCorrection.hpp`,
`src/ScientificRuntime/Python/FnvCorrectionBridge.hpp` (task 86). Plan:
`docs/work/project/plans/2026-10-06-fnv-review.md`, "Plateau: półautomatycznie" - the app proposes a
window, the user drags it, the correction updates without re-running Python.

## Goal

A dockable "FNV Correction" panel: pick the defect LOCPOT (q and the bulk LOCPOT are guessed from
the folders), set epsilon and the centre (typed, from the selected density's localisation centre
of task 85, or from selected atoms), press "Oblicz profile", and get E_lat, C and E_corr with the
three profiles plotted and each axis's plateau window draggable; every drag recomputes C and E_corr
instantly. Warnings say when an axis has no plateau or the charge looks delocalised.

## The contract (already written - do not change signatures or test expectations)

- `src/Presentation/Panels/FnvCorrectionInputs.hpp` + `tests/Presentation/Panels/FnvCorrectionInputsTests.cpp`.

## Do

1. `src/Presentation/Panels/FnvCorrectionInputs.cpp` - implement the header (folder-name regex
   for q, `<ancestor>/bulk/LOCPOT` lookup for the 2nd and 3rd ancestors, `inverse(lattice)`
   with wrap `x - floor(x)`, |det| <= 1e-6 -> nullopt). Use `Path::Utf8()` / `FromUtf8()` for
   anything derived from a user path (non-ASCII folders such as "Praca-Inżynierska").
2. `src/Presentation/Panels/FnvCorrectionPanel.{hpp,cpp}` - an `IPanel`
   (`PanelCategory::Analysis`, model it on `OccupationDiagramPanel`/`GroupTheoryPanel` for the
   panel plumbing and on `SceneDensityLoader` for the job polling: `JobSystem::Submit`, then
   `GetJob(id)` / `JobStatus` each frame, take `GetResult()` on Completed, show the job's error on
   Failed). Registered in `EditorLayer.cpp` next to `GroupTheoryPanel` as
   `registerPanel<FnvCorrectionPanel>(*rendererLayer, m_JobSystem, "FNV Correction", false)`.
   Session-only state (no persistence in this task). Layout, all label/value rows on
   `PropertyGrid` like the density section (labels left, one `PropertyGridLabelWidth` per grid):
   - **Dane** (CollapsingHeader, open):
     - "LOCPOT defektu": path text (`Utf8()`, ellipsised from the left if long) + "..." button
       (`PickOpenFile` with an empty filter, as `SceneDensityEditor` does). On pick: q from
       `ChargeFromCalculationFolder` (if it says something), bulk from `FindBulkLocpotNear` (if the
       bulk field is empty), and the profiles are dropped (they belong to the old file).
     - "LOCPOT bulk": same widget.
     - "Ładunek q": `InputInt`; tooltip "q = -1: dodatkowy elektron. sxdefectalign: --charge = -q".
     - "ε": `InputDouble` default 5.7, "%.3f"; "Źródło ε": `InputText`, default "promotor (diament)".
     - "Środek": `InputDouble3` fractional "%.6f"; under it, buttons "Z gęstości" (enabled when
       the focused renderer window has a selected density whose
       `SceneDensityAnalysisCentreFractional` resolves) and "Z zaznaczonych atomów"
       (`SelectedAtomsCentroid` -> `FractionalCoordinates(windowState.structure.lattice, ...)`).
       Use the same focused-window accessor the ObjectPropertiesPanel uses.
     - "ENCUT": `InputDouble` eV, 0 = "z INCAR" (show that hint as the format when 0).
     - Button "Oblicz profile (krok 1)", full width, disabled while the job runs or without both
       LOCPOTs / with q == 0; while running a "Liczenie... (odczyt LOCPOT ~1 min)" line; on failure
       the job's message in the warning colour.
   - **Wynik** (shown once profiles exist):
     - "Konwencja G=0": combo "sxdefectalign (praca inż.)" (default) / "pymatgen"; changing it
       re-proposes all windows.
     - an ImGui table, one row per axis: "Oś" (a1/a2/a3), "Użyj" (checkbox), "Okno [Å]"
       ("centre ± width/2"), "C [eV]" (%+.4f), "Rozrzut" (%.4f), "Nachylenie" (%.2f), "Plateau"
       (✓ / ✗ text).
     - PropertyGrid values: "E_lat" (%.4f eV), "C" (%+.4f eV, ± spread/2 over used axes),
       "E_corr" (%.4f eV, = E_lat + q C), "Δ (E_corr−E_lat)".
     - warnings (warning colour, wrapped): a used axis without plateau -> "Oś aN: brak plateau -
       przesuń okno albo zmień oś."; mean slope of used axes < 0.7 -> "Nachylenie ΔV/V_model =
       x.xx: ładunek częściowo zdelokalizowany, poprawka FNV przeszacowana."; < 0.3 -> "...
       zdelokalizowany - FNV nie ma sensu.".
     - buttons "Zaproponuj okna" (re-run `ProposeFnvAlignment` on every axis) and "Kopiuj wynik"
       (`ImGui::SetClipboardText` with one TSV line: defect LOCPOT folder, q, ε, ε source,
       centre "x,y,z", E_lat, C, E_corr, convention).
   - **Profile** (shown once profiles exist): `ImGui::BeginTabBar` with tabs a1 / a2 / a3, each an
     ImPlot (full width, ~220 px) of ΔV (orange #eb6834), V_model (blue #2a78d6) and ΔV − V_model
     (green #1baf7a) against the distance from the defect along the axis: x' = (x - defect) mod L
     in [0, L), so the defect sits at both ends and the proposed window in the middle and the
     window never wraps. The window is a shaded band (`ImPlot::PlotShaded` or a `DragRect` without
     y edit) with two `ImPlot::DragLineX` handles for its edges; dragging an edge sets the window
     in natural coordinates (centre = defect + midpoint mod L, width = right - left, clamped to
     >= one grid step) and recomputes that axis with `AlignFnvInWindow`. A horizontal line at C.
     Legend on, outside the plot area, top.
3. Keep every `.cpp` under ~500 lines (split the plot drawing into `FnvCorrectionPanelPlots.cpp`
   if needed).

## Files to create or change

Create: `src/Presentation/Panels/FnvCorrectionInputs.cpp`, `src/Presentation/Panels/FnvCorrectionPanel.{hpp,cpp}`
(+ optional `FnvCorrectionPanelPlots.cpp`). Change: `src/Presentation/EditorLayer.cpp` (registration only).

## Files that must NOT be touched

- the contract header and test, everything under `src/Domain/`, `src/ScientificRuntime/`,
  `scripts/`, `src/Renderer/`
- `src/Presentation/Operators/**`, `OperatorRedoPanel.*`
- `premake5.lua`, `build/`, `install/`, `test-directory/`

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=FnvCorrectionInputs*` green, no contract change.
2. Full Release suite: no new failures.
3. In the app (manual): pick `test-directory/fnv-diamond/okeanos/data/GeV/q-1/LOCPOT` -> q = -1,
   bulk = `.../data/bulk/LOCPOT`; centre from the selected GeV q-1 density; Oblicz; three plots;
   dragging a window edge changes C and E_corr at once.

## Constraints

- Presentation may include Renderer, Domain, ScientificRuntime; nothing includes Presentation.
- The job runs on the JobSystem; only the main thread writes panel state.
- No exceptions on the render path. ImGui IDs unique per axis (PushID).
