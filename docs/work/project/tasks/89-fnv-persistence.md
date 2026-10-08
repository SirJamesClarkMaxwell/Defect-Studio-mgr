# Task 89: FNV corrections are saved with the project

Branch `task/89-fnv-persistence` off `task/88-gl-counter-readback` (which is off `task/87-fnv-panel`).
Follow-up from `85-87-fnv-report.md` ("FNV inputs/results are not saved with the project").

## Goal

The FNV Correction panel (task 87) keeps everything in memory, so closing the app loses the inputs and
a result that took a minute to compute. Every correction the user computes is now kept per defect
LOCPOT in `fnv_corrections.yaml` next to `manifest.yaml`, saved automatically, and restored when the
project opens - the result shows immediately, no LOCPOT re-read.

## The contract (already written - do not change signatures or test expectations)

- `src/IO/FnvCorrectionsIO.hpp` - `PersistedFnvCorrection`, `FnvCorrectionsFile`,
  `UpsertFnvCorrection`, `FnvCorrectionsIO` (schema in the header comment).
- `tests/IO/FnvCorrectionsIOTests.cpp`.

## Do

1. `src/IO/FnvCorrectionsIO.cpp` - implement the header with yaml-cpp, modelled on
   `src/IO/SceneObjectsIO.cpp` and `src/IO/ProjectManifestIO.cpp` (read both: emitter style, flow
   sequences for vectors, temp-file-then-rename Save, `Path::Utf8()`/`Path::FromUtf8()` - never
   `Path::String()`, project paths contain Polish letters).
   - Doubles must round-trip exactly: emit with `YAML::Precision(17)` (or `std::format("{:.17g}")`).
   - Relative path: `lexically_relative` of the normalised native paths when the LOCPOT is under
     `projectDirectory` (no `..` in the result), written with `generic_u8string`-style '/' separators.
   - `convention` written as `Sxdefectalign` / `Pymatgen`.
   - Parse: per-entry try/catch around yaml-cpp conversions; a bad entry is dropped with one
     warning line. Axis positions rebuilt as `k * length / N`. `active` out of range -> -1.
     When an entry is dropped, `active` must still point at the same entry, or -1 if that entry was
     the dropped one (the test drops entry 0 with active 0 -> -1).
2. `src/Events/ProjectEvents.hpp` - add
   `struct FnvCorrectionsChanged final : public BusEvent { FnvCorrectionsFile file; };`
   (include `IO/FnvCorrectionsIO.hpp`; check `ProjectEvents.hpp` may include IO - if the
   architecture check forbids Events -> IO, put the event in a new `src/Events/FnvEvents.hpp`
   instead).
3. `FnvCorrectionPanel` (`src/Presentation/Panels/FnvCorrectionPanel.{hpp,cpp}`):
   - Constructor gains `Ref<EventBus> eventBus` (as `DisplacementComparisonPanel` takes it);
     update the `registerPanel<FnvCorrectionPanel>` call in `EditorLayer.cpp` and `Clone()`.
   - Member `FnvCorrectionsFile m_Saved;` and `bool m_Dirty = false;`.
   - `void SetPersistedState(FnvCorrectionsFile file)` - replaces `m_Saved` and loads
     `m_Saved.corrections[m_Saved.active]` into the panel (or resets the panel to its constructor
     defaults when `active == -1`). Loading an entry: `m_Request` from the inputs, `m_DielectricSource`,
     `m_Convention`, `m_UseAxis`, `m_Profiles = entry.profiles`; with profiles, `m_ModelPotential[a] =
     FnvModelPotential(...)` and `m_Alignments[a] = AlignFnvInWindow(axis, model, entry.windows[a])`
     under the same finite/size guard `proposeWindows()` uses (on failure: no profiles, no crash).
     Cancels nothing - a pending job's result is dropped by bumping `m_InputRevision`. Does NOT
     publish.
   - `currentEntry()` builds a `PersistedFnvCorrection` from the panel (windows from
     `m_Alignments[a].window` when profiles exist). Every change that matters marks `m_Dirty`:
     any input change (where `invalidateProfiles()` is called today), new profiles in `pollJob`,
     convention change, axis checkbox, "Zaproponuj okna", a window drag in
     `FnvCorrectionPanelPlots.cpp`.
   - At the end of `Render()` (also when the window is hidden): if `m_Dirty` and
     `!ImGui::IsMouseDown(ImGuiMouseButton_Left)` and the defect LOCPOT is set, then
     `UpsertFnvCorrection(m_Saved, currentEntry())`, clear `m_Dirty`, and
     `m_EventBus->Queue(FnvCorrectionsChanged{m_Saved})`. Dragging a window thus writes once on release.
     Note: a changed input clears the profiles, so the stored entry then has none - correct, they
     belonged to the old inputs.
   - A "Zapisane" combo as the first row of the "Dane" section, shown when `m_Saved` is not empty:
     one item per entry, label `<parent folder of the defect LOCPOT relative to its grand-grandparent,
     e.g. "GeV/q-1">  q=<charge>  E_corr=<value or "—">` (E_corr from CombineFnvCorrection over the
     saved windows, computed when the combo opens, not per frame). Picking one loads it as in
     `SetPersistedState` and sets `m_Saved.active`. A "Nowe" item resets the panel to defaults with
     `active = -1` (nothing deleted). A small "Usuń" button next to the combo removes the active
     entry from `m_Saved` (active -> -1, panel reset, publish).
   - Picking a defect LOCPOT whose path is already in `m_Saved` loads that entry instead of
     guessing q/bulk again.
4. `EditorLayer`:
   - Subscribe to `FnvCorrectionsChanged` like `DisplacementComparisonStateChanged`
     (`EditorLayer.cpp:1351`, handler next to `onDisplacementComparisonStateChanged`): with an
     active project, `FnvCorrectionsIO::Save(m_ActiveProjectDirectory, event.file, error)`, log a
     warning on failure. Without a project: nothing (session only, as today).
   - In `refreshProjectDependentPanels()` (check every place `m_ActiveProjectDirectory` is set calls it,
     including the recent-project startup path in `EditorLayerProjectScene.cpp`; add the call or a
     separate load where it is missing): `FnvCorrectionsIO::Load`; on success hand the file to the
     panel with `SetPersistedState`; log warnings; on error log and hand an empty file. Keep the
     panel id like `m_DisplacementComparisonPanelId` (`m_FnvCorrectionPanelId`).

## Files to create or change

Create: `src/IO/FnvCorrectionsIO.cpp`. Change: `src/Events/ProjectEvents.hpp` (or new
`src/Events/FnvEvents.hpp`), `src/Presentation/Panels/FnvCorrectionPanel.{hpp,cpp}`,
`src/Presentation/Panels/FnvCorrectionPanelPlots.cpp`, `src/Presentation/EditorLayer.{hpp,cpp}`,
`src/Presentation/EditorLayerProjectScene.cpp` only if needed.

## Files that must NOT be touched

- the contract header and the test file
- everything under `src/Renderer/`, `src/Domain/`, `src/ScientificRuntime/`
- `src/Presentation/Panels/Viewport*`, `src/Presentation/Panels/SceneHideVolumeEditor.*` (another
  task is editing the viewport in parallel)
- `premake5.lua`, `build/`, `install/`, `test-directory/`

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=FnvCorrections*:FnvCorrection*` green.
2. Full Release suite: no new failures. `.cpp` files stay under ~500 lines (split the panel's saved-
   entries UI into `FnvCorrectionPanelSaved.cpp` if `FnvCorrectionPanel.cpp` would pass it).
3. In the app (checked by the user): compute GeV q-1, drag a window, close the app, reopen - the panel
   shows the same E_corr without "Oblicz profile"; computing q+1 then switching back with "Zapisane"
   restores q-1.
