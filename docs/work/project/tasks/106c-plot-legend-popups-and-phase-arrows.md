# Task 106c: legend popups on every plot + arrows that follow the animation phase

## Goal
1. User (2026-10-12): "każdy wykres powinien mieć legend-popup aby móc sobie to jakoś dostosować".
   Right-click on a legend entry opens a popup to restyle that series. Built once as a shared
   helper, used first in the Phonons panel (β(ℏω), S_k i S(ℏω), PL, Dyspersja bulku), persisted
   per project in `phonon_panel.yaml`. Other panels adopt it in a later task.
2. User: "dlaczego strzałki nie podążają fazą za atomami?" - while a mode is animated (and when the
   phase slider is moved while paused) each arrow is the instantaneous displacement direction:
   vector x sin(phase), the same factor as AnimatedPositions (full at pi/2, zero at 0 and pi, flipped at -pi/2; reviewer fixed cos -> sin), recomputed every animation frame.
   Stopped / not animating: the full eigenvector arrow as today. Checkbox "Strzałki zgodne z fazą"
   (default on, persisted) switches back to static arrows.

## Files to create or change
- `src/Presentation/Panels/PlotWidgets.{hpp,cpp}` - the shared helper (search first: if a series
  style struct already exists anywhere in Presentation, reuse/extend it)
- `src/Presentation/Panels/PhononModesPanelPlots.cpp`, `PhononModesPanelDispersion.cpp`,
  `PhononModesPanelView.cpp`, `PhononModesPanel.{hpp,cpp}`
- `src/Presentation/Panels/PhononPanelSession.{hpp,cpp}` (phase-scaled arrows)
- `src/IO/PhononPanelSettingsIO.{hpp,cpp}` (series styles + the checkbox; old files still load)
- tests: `tests/IO/PhononPanelSettingsIOTests.cpp`, `tests/Presentation/Panels/PhononPanelSessionTests.cpp`

## Files that must NOT be touched
- `Vendor/`, `scripts/`, `install/`, `test-directory/`, `premake5.lua`, `src/Renderer/` (the
  arrow geometry stays; the session supplies scaled vectors), other panels

## Helper contract (header first)
```cpp
struct PlotSeriesStyle {
    std::optional<glm::vec4> color;   // nullopt = ImPlot auto colour
    float lineWeight = 1.5f;
    bool visible = true;              // hidden series: legend entry stays, data not drawn
    bool filled = false;              // shaded to zero (lines) - ignored for stems
    float fillAlpha = 0.25f;
    int lineStyle = 0;                // 0 solid, 1 dashed, 2 dotted (ImPlot 1.x: emulate by
                                      // segmenting only if ImPlot has no dash support - check
                                      // Vendor/ImPlot first, drop the option if neither works)
};
// Applies the style to the next ImPlot item (spec / SetNextLineStyle as the vendored ImPlot needs).
void ApplySeriesStyle(const PlotSeriesStyle &style, ImPlotSpec &spec);
// Call right after plotting the item `label`; RMB on its legend entry opens the popup.
// Returns true when the user changed something (caller persists on that frame).
[[nodiscard]] bool SeriesLegendPopup(const char *label, PlotSeriesStyle &style);
```
Popup content: colour (`ColorEdit4`, "auto" checkbox), line weight (`DragFloat` 0.5..6),
visible, filled + alpha, line style, "Przywróć domyślne".

## Persistence
`phonon_panel.yaml`: `seriesStyles: { "<plot>/<series>": {color: [r,g,b,a] | null, weight,
visible, filled, fillAlpha, lineStyle} }` and `arrowsFollowPhase: true`; missing keys ->
defaults; round-trip test. Persist on popup change (no save every frame while dragging: on
deactivate, like the other controls).

## Acceptance criteria
1. Regenerate projects if files are added. Reviewer builds Debug+Release, all tests green except
   the known skips.
2. Session test: with arrowsFollowPhase on and phase = -pi/2, the arrow vectors are the negated
   static ones (within 1e-6); at pi they have length ~0 (and BuildModeArrowSegments skips them);
   off -> unchanged by phase.
3. Every series in the four phonon plots has the popup; the A1 / other-irreps stems, S(ℏω), PL,
   dispersion bands (one style for all bands), selected-mode line.
4. Report what you could not verify.

## Constraints
- Check ImGui 1.92 / vendored ImPlot APIs against their headers; the sandbox cannot build.
- PowerShell built-ins only (Git Bash tools crash in the sandbox). `.cpp` under ~500 lines.
