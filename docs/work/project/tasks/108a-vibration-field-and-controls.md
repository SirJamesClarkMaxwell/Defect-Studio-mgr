# Task 108a: one vibration field for the animator, "Animacja drgań" controls, degenerate-mode mixing

Plan: `docs/work/project/plans/2026-10-14-vibration-animation.md` (slice 108a; user decisions 2026-10-14 there:
degenerate mixing slider in the first version). Pure C++, no Python. Later slices add the ΔQ direction (108b) and
the bulk mode at q (108c/d) as further sources of the same field.

## Goal

The animator in `PhononPanelSession` animates a prepared `VibrationField` (per-atom complex displacement) instead of
reading one defect mode directly. Today's defect-mode animation looks exactly the same. New: when the selected
defect mode belongs to a degenerate pair (e.g. E in C3v), a slider mixes the two partners
(cos θ · e1 + sin θ · e2). The visualisation section becomes "Animacja drgań" with a source selector (only
"mod defektu" active now; the other entries visible, disabled, with the reason).

## Design (binding)

1. **Domain** (new `src/Domain/Vibrations/VibrationField.{hpp,cpp}`, no UI/renderer includes):
   ```cpp
   // Physical displacement of each atom, complex: at phase φ the displacement is
   // real_i * sin(φ) + imag_i * cos(φ) (imag empty = real field; today's defect modes are real).
   struct VibrationField
   {
       std::string label;                      // e.g. "mod 57 · E · 151.2 meV" or "mod 57/58 · E · θ 30°"
       std::vector<std::string> symbols;       // window matching, same order as positions
       std::vector<glm::dvec3> equilibriumAngstrom;
       std::vector<glm::dvec3> real, imag;     // Å, arbitrary common scale
       std::vector<double> participation;      // per atom, sums to 1 (arrow threshold); empty = uniform
   };
   // Largest |displacement| any atom reaches over a whole cycle (semi-major axis of each atom's ellipse);
   // 0 for an empty/zero field.
   [[nodiscard]] double FieldCycleMaximum(const VibrationField &field);
   // amplitudeAngstrom * d_i(φ) / FieldCycleMaximum: the atom that moves most swings by exactly the amplitude,
   // with one normalisation for the whole cycle. Zero field -> zeros.
   [[nodiscard]] std::vector<glm::dvec3> FieldDisplacements(const VibrationField &field, double phaseRadians,
       double amplitudeAngstrom);
   // Arrows along the displacement at `phase` (nullopt = the real part), longest arrow over ALL atoms =
   // maxLengthAngstrom, atoms below the participation threshold left out (same rules as BuildModeArrows).
   [[nodiscard]] std::vector<ModeArrow> FieldArrows(const VibrationField &field, std::optional<double> phaseRadians,
       double participationThreshold, double maxLengthAngstrom);
   // Defect mode (optionally mixed with its degenerate partner by angle θ, mixing the mass-weighted
   // eigenvectors e = cos θ e1 + sin θ e2, then u_i = e_i / sqrt(m_i), participation |e_i|^2). Translation or
   // out-of-range mode -> empty field.
   [[nodiscard]] VibrationField DefectModeField(const PhononAnalysisResult &result, std::size_t mode,
       std::optional<std::size_t> partner = std::nullopt, double mixAngleRadians = 0.0);
   // The other member of a 2-fold degenerate pair: an ADJACENT non-translation mode with the same non-empty
   // irrep, or (irreps empty) |Δω| <= toleranceMeV. 3-fold: the next member (ponytail: two-member mixing only,
   // note it in the label). nullopt otherwise.
   [[nodiscard]] std::optional<std::size_t> DegeneratePartner(const PhononAnalysisResult &result,
       std::size_t mode, double toleranceMeV = 0.5);
   ```
   `AnimatedPositions` / `BuildModeArrows` stay (existing tests) - optionally reimplemented through the field.
2. **Session** (`PhononPanelSession`): holds the current field, rebuilt when the selected mode, the result or
   the mix angle changes (never per frame). `StartAnimation` / `AdvanceAnimation` / `SetPhase` / `StopAnimation`
   / `ApplyArrows` keep their signatures and the rest-position contract; they read the field. Mix angle: session
   state, reset to 0 when the selected mode changes; `SetMixAngle(double radians)` / `MixAngle()` /
   `DegeneratePartnerOfSelected()`. While animating, changing the angle updates the next frame (no stop).
3. **UI** (`PhononModesPanelView.cpp`, `drawVisualisation`): the section title becomes `Animacja drgań`; first
   row `Źródło` combo: `mod defektu` (active), `kierunek ΔQ` and `mod bulku w q` disabled with tooltip
   `Wymaga wektora ΔR z analizy CCD (następna wersja).` / `Wymaga modu bulku w punkcie q (następna wersja).`;
   below it the field label. When the selected mode has a partner: `Zdegenerowany z modem N` and a `Mieszanie θ`
   DragFloat in degrees 0..180 (tooltip: `cos θ · e1 + sin θ · e2; 0° = wybrany mod, 90° = partner`).
   Existing controls (arrows, colours, play/pause/stop, threshold, amplitude, period) unchanged. Labels left,
   tooltips, disabled controls say why.

## Files to create or change

- new `src/Domain/Vibrations/VibrationField.{hpp,cpp}`; `src/Domain/Vibrations/PhononModes.{hpp,cpp}` only if
  reimplementing through the field
- `src/Presentation/Panels/PhononPanelSession.hpp`, `PhononPanelSession.cpp` (animator), `PhononPanelSessionWindow.cpp`
  if the arrows code lives there, `PhononModesPanelView.cpp`
- tests: new `tests/Domain/Vibrations/VibrationFieldTests.cpp`; additions to the existing animation session tests
  (search `StartAnimation` in `tests/`)

## Files that must NOT be touched

- `src/IO/*`, `src/ScientificRuntime/*`, `scripts/python/*`, renderer, structure-building files (task 112g runs
  in parallel: `CellEditing*`, `CellPanel*`, `Viewport*`, `RendererPanelContextMenu.cpp`, `RendererWindowState.hpp`)
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-108a-vibration-field.md`

## Acceptance criteria (GoogleTests)

1. Regression: for a synthetic defect result, `FieldDisplacements(DefectModeField(r, m), φ, A)` equals
   `AnimatedPositions(r, m, φ, A) - positions` for several φ; `FieldArrows(field, nullopt, t, L)` equals
   `BuildModeArrows(r, m, t, L)`.
2. Complex field: one atom with real = (1,0,0), imag = (0,1,0) -> circular motion, |d| = A at every φ;
   `FieldCycleMaximum` = 1; an elliptic case (real (2,0,0), imag (0,1,0)) normalises by 2 at every φ.
3. Mixing: θ = 0 gives mode 1, θ = 90° gives mode 2 (up to sign), θ = 45° of an orthonormal pair keeps
   participation summing to 1.
4. `DegeneratePartner`: E pair at 100.0 / 100.2 meV -> each other; A1 neighbour -> none; empty irreps uses the
   tolerance; translations never.
5. Session: animating, then `SetMixAngle` -> next `SetPhase` writes the mixed frame; `StopAnimation` restores the
   rest positions exactly; selecting another mode resets the angle. Existing tests unchanged and green.
6. Report: files, tests, anything not done.

## Constraints

- Domain stays UI-free; only the main thread writes window positions (as today).
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- `.cpp` < ~500 lines; regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
