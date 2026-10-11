# Task 85: density centre and localisation - FNV plan step 3

Branch `task/85-density-centre` off `task/84-hide-volumes` (it reuses the hide-volume sphere mesh
for the viewport overlay). Plan: `docs/work/project/plans/2026-10-06-fnv-review.md`, "Poprawki do
planu" 1 - CHGCAR in the MVP to pick the FNV `--center` and to check localisation.

## Goal

A loaded density (spin density, or q minus q0 difference) proposes the defect centre - the centroid
of |value| - the moment it loads. The N panel shows that centre in Angstrom and in the fractional
"x,y,z" sxdefectalign takes after `--center`, lets the user override it (type, take the selected
atoms' centroid, back to the centroid), and shows how much of |value| sits within a radius R: the
number, R50/R90, a cumulative curve with R draggable on it, and a warning when the state is
delocalised (FNV meaningless). While the density is selected the R sphere is drawn in the viewport.

## The contract (already written - do not change signatures or test expectations)

- `src/Domain/Electronic/DensityAnalysis.hpp` - centroid + radial profile maths.
- `src/Domain/Electronic/DensityGrid.hpp` - `DensityGrid::centroid`.
- `src/Renderer/Scene/SceneDensityAnalysis.hpp` - centre resolution, profile cache, formatting.
- `src/Renderer/RendererWindowState.hpp` - `SceneDensity::analysisCentre`, `analysisRadius`, cache fields.
- Tests: `tests/Domain/Electronic/DensityAnalysisTests.cpp`, `tests/Renderer/SceneDensityAnalysisTests.cpp`,
  the new case in `tests/ScientificRuntime/VaspDensityGridBridgeTests.cpp`.

## Do

1. `src/Domain/Electronic/DensityAnalysis.cpp`: implement the header exactly as documented
   (circular mean + one minimum-image refinement per axis, uniform axis -> 0.5; radial profile in
   one pass, double accumulators; `FractionWithin` / `RadiusHolding` by linear interpolation inside
   a bin, as documented). No threads, no allocation per point.
2. `VaspDensityGridBridge.cpp`: after the values are read, `density.centroid =
   AbsoluteDensityCentroid(density.grid)`. It runs inside the job, so on the worker thread.
3. `src/Renderer/Scene/SceneDensityAnalysis.cpp`: implement the header. Fractional = `inverse(cell)
   * (centre - origin)`, wrapped with `x - floor(x)`; a singular cell (|det| <= 1e-6) -> nullopt.
4. Persistence: `PersistedSceneDensity` gains `std::optional<glm::vec3> analysisCentre` and
   `float analysisRadius = 5.0f`; YAML keys `analysisCentre` (emitted only when set) and
   `analysisRadius` in `ParseDensity` / `EmitDensity`; mapping both ways in
   `SceneObjectPersistence.cpp`. Old files without the keys load with the defaults.
5. N panel: new `src/Presentation/Panels/SceneDensityLocalisation.{hpp,cpp}` with
   `void DrawSceneDensityLocalisation(RendererWindowState &, RendererWindowState::SceneDensity &, float labelWidth)`,
   called from `DrawSelectedSceneDensitySection` as a new `CollapsingHeader("Lokalizacja (FNV)")`
   after "Statystyki", open by default, shown only when `data` is loaded. Same `PropertyGrid`
   style as the rest of the section; include the new labels in its `PropertyGridLabelWidth`.
   Rows:
   - **Środek**: combo "Centroid |ρ|" / "Ręczny" (Ręczny = `analysisCentre` set; choosing
     Centroid resets it).
   - **Pozycja**: DragFloat3, " Å", the resolved centre; an edit sets `analysisCentre`.
   - **--center**: read-only `FormatSxdefectalignCentre(fractional)` + small "Kopiuj" button
     (`ImGui::SetClipboardText` with the bare "x,y,z"); tooltip: "Współrzędne ułamkowe dla
     sxdefectalign --relative --center".
   - a row of two buttons: "Z zaznaczonych atomów" (`SelectedAtomsCentroid`, disabled without a
     selection) and "Centroid" (reset).
   - **Promień R**: DragFloat 0.5 .. 20 Å.
   - **W kuli R**: "87.3 % |ρ|" (`FractionWithin`); **R50 / R90**: "2.10 Å / 4.80 Å" (`RadiusHolding`).
   - below the grid, when `FractionWithin(R) < kLocalisedFractionWarning`: a warning-coloured,
     wrapped line "Gęstość zdelokalizowana - poprawka FNV dla tego stanu nie ma sensu."
   - an ImPlot line plot (full width, ~140 px high, no legend): x = r in Å from 0 to
     `max(2 R, 1.5 R90)`, y = cumulative fraction 0..1 (sample `FractionWithin` at ~200 points);
     `ImPlot::DragLineX` at R that writes `analysisRadius` back. Check how ImPlot is already used
     in the repo (`ImPlot::BeginPlot`) and copy that setup.
   Every edit: `PushPinnedMeasurementUndoSnapshot(windowState)` once on `ImGui::IsItemActivated()`
   (buttons: on click), as the rest of the section does.
6. Viewport: in `OpenGlDensityRenderer.cpp`, for each SELECTED, visible density whose centre
   resolves, draw the analysis sphere: a temporary `SceneHideVolume` (Sphere, Anchored, centre,
   `halfExtents.x = analysisRadius`) -> `BuildHideVolumeTriangles` -> the same translucent overlay
   call `renderSceneHideVolumes` uses (white, alpha 0.12). Reuse that helper; if it is private to
   `OpenGlHideVolumeRenderer.cpp`, move the shared "upload triangles + draw overlay" part into a
   private `OpenGlRendererBackend` member both call. No new shader, no new pass.
7. `SceneDensityLoader.cpp`: when a grid arrives, log the centroid (fractional, 4 decimals) next to
   the existing statistics line.

## Files to create or change

Create: `src/Domain/Electronic/DensityAnalysis.cpp`, `src/Renderer/Scene/SceneDensityAnalysis.cpp`,
`src/Presentation/Panels/SceneDensityLocalisation.{hpp,cpp}`.

Change: `src/ScientificRuntime/Python/VaspDensityGridBridge.cpp`, `src/IO/SceneObjectsIO.hpp`,
`src/IO/SceneObjectsYaml.cpp`, `src/Renderer/Scene/SceneObjectPersistence.cpp`,
`src/Presentation/Panels/SceneDensityEditor.cpp`, `src/Presentation/Panels/SceneDensityLoader.cpp`,
`src/Renderer/OpenGl/OpenGlDensityRenderer.cpp`, and only if step 6 needs it
`src/Renderer/OpenGl/OpenGlHideVolumeRenderer.cpp` + `OpenGlRendererBackend.hpp`.

## Files that must NOT be touched

- the contract headers and every test file listed above
- `src/Renderer/Scene/SceneHideVolume.*`, `SceneHideVolumeEditor.*`, `ViewModifier.cpp`,
  `HiddenSceneState.*`, `SceneVisibility.*`
- `src/Presentation/Operators/**`, `OperatorRedoPanel.*`
- `scripts/python/**` (the loader script stays as it is - the centroid is C++)
- `premake5.lua`, `build/`, `install/`

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=DensityAnalysis*:SceneDensity*:VaspDensityGridBridge*`
   all green, no expectation or contract signature changed.
2. Full Release suite: no new failures.
3. `DensityAnalysis.cpp` includes nothing outside Domain/Core/glm/std; `SceneDensityAnalysis.cpp`
   nothing from Presentation/ImGui.
4. In the app on GeV q-1 (`test-directory/fnv-diamond/okeanos/data/GeV/q-1/CHGCAR`,
   magnetization): the proposed centre lands on the Ge site (within ~0.1 Å), the fractional
   string is close to the thesis `0.468766,0.531233,0.468766` (that was q-2; the site is the
   same), and W kuli R at R = 5 Å is well above the warning. Reference numbers from a numpy
   run of the same algorithm: GeV q-1 centroid (0.46872, 0.53126, 0.46865), 93.4 % within 5 Å;
   Ge_C q+1 21 % (warning shown).

## Constraints

- Domain: no Renderer/Presentation/IO includes. Renderer: no ImGui.
- No exceptions in render paths. The profile is never computed per frame without the cache.
- `glm` + ImPlot (already linked) only; no new dependency.
