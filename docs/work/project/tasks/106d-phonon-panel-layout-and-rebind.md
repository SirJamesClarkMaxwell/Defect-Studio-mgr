# Task 106d: Phonons panel - visualisation layout, visible current row, window rebind, trimmed spectra

User feedback 2026-10-13 on the running 106c build.

## Goal
1. **Window rebind after app restart.** "jak ładujemy aplikację, to aplikacja nie rozpoznaje już
   załadowanego okna do fononów i każe sobie utworzyć nowe". After a project load the restored
   renderer window that shows `supercellPath()` must be bound automatically: in `syncWindow()`, when
   `m_WindowId` is empty and no bind is pending, bind to the first window whose
   `structure.sourcePath` refers to the same file as `supercellPath()` (compare normalised paths,
   case-insensitive on Windows - check how restored windows store `sourcePath`, it may be relative or
   differently normalised; put the comparison in a small static helper in `PhononPanelSession` so it
   is unit-testable). Arrows appear without pressing "Otwórz superkomórkę".
2. **Visualisation section layout** (`drawVisualisation`, screenshot feedback): labels on the
   left / above, never the ImGui default right-hand labels; info and basic settings first; related
   sliders side by side in one row with their captions above them (borderless `ImGui::BeginTable`,
   `ImGuiTableFlags_SizingStretchSame`, no header styling - plain `TextUnformatted` captions);
   colours and start/stop on one line. Target layout:
   ```
   [Otwórz superkomórkę]  Superkomórka (fonony)            <- or the window error
   [x] Pokaż strzałki  [x] Zgodne z fazą   Kolory: [■] [■]   [▶ Animuj] [⏹ Stop]
   Próg udziału          | Atomy niosące [%]       | Długość strzałki [Å]
   [====slider====]      | [====slider====]        | [==drag==]
   strzałek: 36 z 162    | 162 atomów niesie 100 % |
   Amplituda [Å]         | Okres [s]               | Faza
   [==drag==]            | [==drag==]              | [====slider====]  (disabled unless paused)
   ```
   Widgets use `##id` labels and `SetNextItemWidth(-FLT_MIN)` inside cells. Keep every tooltip
   (move them to the captions + widgets). Colour swatches keep tooltips "Mały udział" / "Duży udział".
   Behaviour and persistence unchanged.
3. **Current row clearly marked** in the mode table: "dobrze by było aby wiersz w tabeli z fononami
   też się jakoś odznaczał który akurat wyświetlamy". The current `RowBg1 = HeaderActive` is not
   visible in the dark theme. Use the accent colour (`ImGuiCol_CheckMark`, the orange of the
   checkboxes) at ~0.35 alpha as `ImGuiTableBgTarget_RowBg0` AND `RowBg1` for the selected row, and
   prefix the index cell with "▶ " while the mode is being animated (session `IsAnimating()`),
   "● " otherwise. Keyboard up/down + scroll-to-row stay.
4. **Spectra trimmed to the useful range** ("nie wiem czy potrzeba wyświetlać aż tak długi S(hw)"):
   the S(ℏω) grid in the result runs to ~3000 meV and PL to wherever the grid ends, so ImPlot
   auto-fit (double click) zooms out to an empty plot. Plot only the samples inside
   `ModeEnergyRangeMeV()` for S(ℏω) and inside `PlEnergyRangeEv()` for PL (one sample margin on each
   side), so auto-fit lands on the same range as the default view. Add a session helper returning
   the index range `[first, last)` of a sorted grid inside `[min, max]` and unit-test it.

## Files to create or change
- `src/Presentation/Panels/PhononModesPanel.cpp` (`syncWindow`)
- `src/Presentation/Panels/PhononModesPanelView.cpp` (table row, `drawVisualisation`)
- `src/Presentation/Panels/PhononModesPanelPlots.cpp` (trimmed series)
- `src/Presentation/Panels/PhononPanelSession.{hpp,cpp}` (two static helpers: same-file path
  comparison, grid index range)
- `tests/Presentation/Panels/PhononPanelSessionTests.cpp` (tests for both helpers)

## Files that must NOT be touched
- `Vendor/`, `scripts/`, `install/`, `test-directory/`, `premake5.lua`, `src/Renderer/`, `src/IO/`,
  other panels, `PlotWidgets.*` (use it, do not change it)

## Acceptance criteria
1. No new files -> no project regeneration. Reviewer builds Debug+Release, all tests green except
   the known skips.
2. Path helper test: `C:/a/b/POSCAR` vs `c:\a\b\.\POSCAR` same; vs `C:/a/c/POSCAR` different; empty
   never matches.
3. Grid helper test: grid 0..10 step 1, range [2.5, 6.2] -> indices covering 2..7 (one-sample
   margin), clamped at the ends, empty grid -> empty range.
4. Report what you could not verify (UI layout is checked by the reviewer in the app).

## Constraints
- ImGui 1.92 APIs: check `Vendor/imgui/imgui.h` for table flags; the sandbox cannot build.
- PowerShell built-ins only (Git Bash tools crash in the sandbox). Do not edit UTF-8 files with
  `Get-Content`/`Set-Content` (mojibake) - use the apply-patch / file edit tool. `.cpp` under ~500 lines.
- Only the main thread touches renderer windows (it already does - `syncWindow` runs in OnImGuiRender).
