# Task 106b: Phonons panel - user feedback round 1 + bulk dispersion view

## Goal
Fix what the user reported after the first real use of the Phonons panel (2026-10-12) and show the
bulk phonon dispersion (script from task 106a, `scripts/python/examples/phonon_dispersion.py`)
inside the same panel. Every new setting persists like the existing ones (save on edit end,
nothing runs by itself).

## Files to create or change
- `src/Presentation/Panels/PhononModesPanel*.cpp`, `PhononModesPanel.hpp` (private members)
- new `src/Presentation/Panels/PhononModesPanelDispersion.cpp` (keep every .cpp under ~500 lines)
- `src/Presentation/Panels/PhononPanelSession.{hpp,cpp}` only if needed
- `src/IO/PhononPanelSettingsIO.{hpp,cpp}` - new keys (see 7), old files must still load
- new `src/IO/PhononDispersionIO.{hpp,cpp}` - reader of the 106a output directory
- new `src/ScientificRuntime/Python/PhononDispersionJob.{hpp,cpp}` - follow
  `CcdInputsJob.{hpp,cpp}` (ScriptRunner, JSON last line, cancel)
- `src/Renderer/ModeArrows.{hpp,cpp}` (+ its test) for item 2
- `src/Presentation/EditorLayer*.cpp` only to hand the project-tree roots to the panel (item 1)
- tests: `tests/IO/PhononDispersionIOTests.cpp` (new), `tests/IO/PhononPanelSettingsIOTests.cpp`
  (extend), `tests/Renderer/ModeArrowsTests.cpp` (extend)

## Files that must NOT be touched
- `Vendor/`, `premake5.lua`, `install/`, `test-directory/`, `scripts/` (the 106a script is done)
- `src/Presentation/ImGuiLayer.cpp` (the missing ℏ glyph is already fixed there by the reviewer:
  Cambria Math merged as fallback)

## Items
1. **Paths relative to the project (user: "już ci mówiłem: ścieżki względem projektu").**
   Every directory field in the panel (FC, gs, es, triplet, result, bulk FC, CCD, dispersion)
   DISPLAYS the path relative to the project directory when inside it, else relative to the
   project-tree root containing it shown as `<root label>/<rest>` (roots: `ProjectRootEntry`,
   see how `EditorLayer.cpp` passes `currentRootsMutable()` to the calculator console), else
   absolute. Full absolute path in the tooltip. Typing a relative path resolves against the project
   directory. Storage in `phonon_panel.yaml` keeps today's rule (`StoredPath`). Trim of
   spaces/quotes in `DrawFolder` stays.
2. **Arrows start at the atom surface, not its centre** (they pierce the sphere today): in
   `BuildModeArrowSegments` shift `base` by the drawn atom radius along the arrow direction and
   keep `tip - base` = the vector (so the visible length is unchanged). Find where the renderer
   gets the atom display radius for `RendererStructureData`; if it is not reachable there, add a
   `float radiusAngstrom` per atom to the call site rather than guessing. Update the header
   comment and the test.
3. **Mode table:** the row of the selected mode is clearly highlighted
   (`ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0/1, accent)`), and when a mode is picked from
   a plot the table scrolls to it.
4. **Plots - sensible default ranges** (user zoom must still work: set limits with
   `ImPlotCond_Always` only on the frame a new result is loaded or the tab/ZPL changes, otherwise
   `ImPlotCond_Once`): β(ℏω) and S_k: x from min(0, lowest mode) - 5 to 1.1 x highest mode; PL:
   x from the lowest energy where the curve exceeds 1e-3 to ZPL + 0.15 eV.
5. **Visualisation controls - the user did not understand them:**
   - "Próg udziału" -> label "Ukryj strzałki poniżej udziału" + tooltip: "Udział atomu w modzie =
     |e_i|^2 (suma po atomach = 1). Strzałki atomów o mniejszym udziale są ukryte.";
   - "X [%]" -> "Zlicz atomy niosące [%]" + tooltip: "Ile najsilniej drgających atomów razem niesie
     X % modu (kolumna N(X%) w tabeli).";
   - "Długość najdłuższej strzałki [Å]", "Amplituda [Å]", "Okres [s]" -> `DragFloat`
     (ranges 0.1..5 Å speed 0.01; 0..1 Å speed 0.005; 0.2..10 s speed 0.02, AlwaysClamp);
   - colour pickers -> `ColorEdit3(..., ImGuiColorEditFlags_NoInputs)` (swatch only).
6. **"Dyspersja bulku" collapsing header** (new file): FC directory = the existing
   `bulkForceConstantDirectory` (same RMB role "bulk"), checkbox "tylko w płaszczyźnie (2D)"
   (script `--in-plane`, default on), `npoints` (default 101), output directory default
   `<project>/phonons/bulk_dispersion`, button "Policz dyspersję" (off the main thread via
   `PhononDispersionJob`, cancel button, error text as is), "Wczytaj..." for an existing result.
   Plot: ImPlot line per band per continuous segment (one colour), vertical lines + labels at
   `ticks`, a darker line at `breaks`, y label ℏω [meV], zero line, x without numbers.
   On load show "pasm: N, max ℏω = X meV". Loads the saved result when the project opens.
7. **Persistence:** new keys `dispersionDir`, `dispersionInPlane`, `dispersionPoints` in
   `phonon_panel.yaml`; missing keys -> defaults; round-trip test.

## PhononDispersionIO contract (write the header first)
```cpp
struct PhononDispersionTick { double distance = 0.0; std::string label; };
struct PhononDispersion {
    int bandCount = 0;
    std::vector<int> segment;                 // per q-point
    std::vector<double> distance;             // per q-point, non-decreasing
    std::vector<std::vector<double>> bandsMeV; // [band][q-point]
    std::vector<PhononDispersionTick> ticks;
    std::vector<double> breaks;
    double minMeV = 0.0, maxMeV = 0.0;
    bool inPlane = false;
};
namespace PhononDispersionIO {
    // Reads summary.json (schema ds.phonon_dispersion.v1) + dispersion.tsv; false + error text on
    // a missing file, wrong schema, column count != bandCount + 2, non-finite value.
    [[nodiscard]] bool Load(const Path &directory, PhononDispersion &out, std::string &error);
}
```
Tests: a tiny hand-written directory in a temp dir (2 bands, 2 segments) loads; wrong schema,
missing file and a short row fail; the real 106a output (run the script into a temp dir from the
test only if that is how the other ScientificRuntime end-to-end tests do it, else skip).

## Acceptance criteria
1. Regenerate projects (`scripts/Windows/GenerateProjects.bat`, new files). The reviewer builds
   Debug+Release and runs `DefectStudioTests.exe`; everything green except the known skips.
2. No `.cpp` over ~500 lines; no layer violations (IO does not include Presentation, Renderer does
   not include IO/Presentation).
3. Report every item 1-7 as done / not done, with file:line of the change.

## Constraints
- Building: the sandbox cannot write `build/`; check every API you call against its header
  (ImGui 1.92, ImPlot in `Vendor/`). Use PowerShell built-ins (Git Bash tools crash here).
