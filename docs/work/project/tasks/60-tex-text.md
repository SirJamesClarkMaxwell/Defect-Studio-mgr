# Task 60: scene text with TeX markup and a Text tool

Branch `task/60-tex-text` (from `task/59-manual-test-fixes`). Requested 2026-10-04: an independent
text object for figure labels like V_B, V_N^-, E_g^{(1)}, like a bond label but free, with TeX
support and its own icon in the viewport's vertical toolbar.

Design decision: the text object IS the existing `RendererWindowState::FreeLabel` (MSDF render,
selection, gizmo/G/R/S, Outliner, Object Properties, persistence, undo all exist). Do not create a
second text system. This task adds (A) markup, (B) anchors, (C) the tool and editing UI.

## Goal

1. A free label's text is TeX-like markup (`src/Renderer/Text/TexMarkup.hpp` documents the grammar)
   drawn with real sub/superscripts, Greek and symbols by the MSDF label renderer.
2. A label can follow an atom or a vacancy (`src/Renderer/Scene/SceneFreeLabelAnchors.hpp`), and
   the anchor is saved in scene_objects.yaml.
3. A "Text" tool in the viewport's vertical toolbar: click places a text (anchored to the atom or
   vacancy under the mouse, otherwise free at the clicked point) and opens an inline editor;
   double-click on a free label edits it; "Vacancy labels" in the Add menus labels vacancies.

## Part A - markup (pure + renderer)

- Implement `src/Renderer/Text/TexMarkup.cpp` against the header (contract, do not change it) and
  `tests/Renderer/Text/TexMarkupTests.cpp` (contract, do not change expectations).
- `src/Renderer/OpenGl/MsdfFont.cpp`: extend `kCharsetRanges` with what the markup can emit beyond
  Latin-1: Greek U+0391-U+03A9 and U+03B1-U+03C9 (plus U+03D1, U+03D5, U+03F5 if you map
  \vartheta/\varphi/\epsilon variants), U+2009, U+2032, U+210F, U+2190-U+2195, U+2212, U+2213,
  U+221E, U+2248, U+2260, U+2264, U+2265, U+27E8, U+27E9. A glyph the font lacks is skipped (the
  existing `glyph.found` path) - do not add a fallback font.
- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp`: free labels (the `freeLabels` loop in
  renderLabels, currently `ToU32String(label.text)`) go through ParseTexMarkup + LayoutTexGlyphs:
  each placed glyph is one OpenGlLabelInstance at x * kWorldFontSize, baseline shifted by
  baselineShift * kWorldFontSize, size multiplied by its scale; the line is centred on its width as
  today; the returned LabelLocalBounds must cover every placed glyph (background quad and the label
  pick rect use it). Refactor `AppendLabelInstances` so plain strings (bond/angle labels) and markup
  share one glyph-emitting loop - plain text is simply glyphs with scale 1 and shift 0. Bond and
  angle labels must render exactly as before.

## Part B - anchors

- Fields already added to `RendererWindowState::FreeLabel`: `anchorAtom`, `anchorVacancy`,
  `anchorOffset`.
- Implement `src/Renderer/Scene/SceneFreeLabelAnchors.cpp` against its header; tests in
  `tests/Renderer/Scene/SceneFreeLabelAnchorsTests.cpp` are the contract.
- Call `RefreshAnchoredFreeLabels(windowState)` right after `SceneSystem::RefreshAnchoredSceneArrows`
  in `src/Presentation/Panels/ViewportInteraction.cpp`.
- `src/Renderer/Scene/SceneTransform.{hpp,cpp}`: translating an anchored free label writes
  `anchorOffset` (desired position - anchor position) as well as worldPosition; restore puts the
  start offset back. You may add a field to `LabelTransformStart` for that.
- Persistence: `PersistedFreeLabel` in `src/IO/SceneObjectsIO.hpp` gets `anchorAtoms`
  (`std::vector<PersistedAtomRef>`, 0 or 1 entries, same as the arrow anchors), `anchorVacancy`
  (`std::optional<int>`) and `anchorOffset`; YAML keys optional (old files load unchanged); wire
  them in `src/IO/SceneObjectsIO.cpp` and `src/Renderer/Scene/SceneObjectPersistence.cpp`
  (`PersistOptionalAtomReference` / `ResolveOptionalAtomReference`, as for arrows). Also add a
  round-trip case for the new keys to `tests/IO/SceneObjectsIOTests.cpp` or a new
  `tests/IO/SceneObjectsFreeLabelAnchorIOTests.cpp`.

## Part C - tool and UI (Presentation)

- `SelectionToolMode::Text` in `src/Renderer/RendererTypes.hpp` (mutually exclusive with the other
  tools like Cursor3D/Measure). A button in the vertical toolbar (`src/Presentation/Panels/
  ViewportToolbars.cpp` or the vertical toolbar file it uses - find where `tool-cursor3d.png` is
  used) with a new icon `install/app/assets/icons/tool-text.png`, drawn with Pillow in the style and
  size of the existing `tool-*.png` (a serif "T", or "T" with a small subscript). Tooltip
  "Text (TeX: V_B, x^2, \\alpha)".
- Text tool click in the viewport: atom under the mouse -> label anchored to it; vacancy under the
  mouse (`PickVacancyAlongRay`) -> anchored to it; else free at the click's world point (the same
  computation the context menu uses for its click position). Push the label undo snapshot
  (`PushPinnedMeasurementUndoSnapshot`), select the new label, and open the inline editor.
- Inline editor: a small ImGui popup at the label's screen position with a single-line InputText
  (focused on open), Enter commits, Escape cancels (a label created by this click and still empty
  is removed on cancel), one undo step per edit. Double-click on a free label (in
  `src/Presentation/Panels/ViewportLabelInteraction.cpp`, which already hit-tests free labels with
  `labelPickQuads`) opens the same editor. Put the editor in a new file
  `src/Presentation/Panels/ViewportTextEditor.{hpp,cpp}`.
- Object Properties (`DrawSelectedLabelProperties` in
  `src/Presentation/Panels/ObjectPropertiesPanelSections.cpp`): for exactly one selected free label,
  an InputText for the text (one undo snapshot when the edit starts), a "(?)" tooltip listing the
  markup, and an "Anchor: atom 12 (C) / vacancy V_B / free" line with a "Detach" button (keeps the
  current position: worldPosition stays, anchors cleared, offset zeroed).
- Add menus (`DrawDefectAddItems` in `src/Presentation/Panels/ViewportVacancyAdd.cpp`, used by
  Shift+A and right-click Add): item "Vacancy labels" - `AddVacancyLabels` for the selected
  vacancies (none selected: all), one undo snapshot, select the new labels.
- The Scene Outliner row of a free label shows its text (markup as typed).

## Files to create or change

- create: `src/Renderer/Text/TexMarkup.cpp`, `src/Renderer/Scene/SceneFreeLabelAnchors.cpp`,
  `src/Presentation/Panels/ViewportTextEditor.{hpp,cpp}`, `install/app/assets/icons/tool-text.png`,
  an IO round-trip test file if you prefer it to editing an existing one
- change: `src/Renderer/OpenGl/MsdfFont.cpp`, `src/Renderer/OpenGl/OpenGlRendererBackend.cpp`,
  `src/Renderer/Scene/SceneTransform.{hpp,cpp}`, `src/Renderer/RendererTypes.hpp`,
  `src/IO/SceneObjectsIO.{hpp,cpp}`, `src/Renderer/Scene/SceneObjectPersistence.cpp`,
  `src/Presentation/Panels/ViewportInteraction.cpp`, `src/Presentation/Panels/ViewportLabelInteraction.cpp`,
  the vertical toolbar file, `src/Presentation/Panels/ObjectPropertiesPanelSections.cpp`,
  `src/Presentation/Panels/ViewportVacancyAdd.cpp`, the Scene Outliner rows file, and
  `src/Presentation/Panels/RendererPanel.cpp` only to hook the editor/tool click if needed.

## Files that must NOT be touched

- `src/Renderer/Text/TexMarkup.hpp`, `src/Renderer/Scene/SceneFreeLabelAnchors.hpp` (contracts)
- `tests/Renderer/Text/TexMarkupTests.cpp`, `tests/Renderer/Scene/SceneFreeLabelAnchorsTests.cpp`
  (contracts - expectations must not change)
- `src/Domain/**`, `src/App/**`, `Vendor/**`, `install/users/**`, anything under `src/Renderer/Path/**`
- Pinned-measurement (bond/angle label) behaviour and look

## Acceptance criteria

1. `DefectStudioTests.exe --gtest_filter=TexMarkupTests.*:SceneFreeLabelAnchorsTests.*` all pass.
2. The full suite has no new failures (known: 5 `PathStrokeMesherTests` bevel cases, 1-2 skips).
3. A free label with text `V_N^-` draws N as a lowered smaller glyph and a raised minus sign;
   `\alpha` draws alpha; bond labels look unchanged.
4. Text tool: click on an atom, type `C_1`, Enter -> label on the atom that follows it when the atom
   is moved; Ctrl+Z removes it; double-click edits it.
5. Save + reopen keeps anchored labels anchored.

## Constraints

- Layers (AGENTS.md / CLAUDE.md): `Renderer/Text` and `Renderer/Scene` are pure - no ImGui, no GL.
  Presentation may read Renderer. Domain untouched.
- No exceptions in render paths. `.cpp` files stay under ~500 lines (OpenGlRendererBackend.cpp is
  already over; add the new glyph layout code to a new small file if it grows it).
- Regenerate projects after adding files: `set DS_TOOLSET=msc-v143` then
  `scripts\Windows\GenerateProjects.bat`. Build: `D:\Aplications\VisualStudio\VisualStudioIDE\MSBuild\Current\Bin\MSBuild.exe
  build\generated\vs2022\DefectStudioTests.vcxproj /p:Configuration=Release /p:Platform=x64 /m:4 /nr:false /v:minimal`
  (one build at a time). If the toolchain is not reachable from your sandbox, say so and skip the
  build; do not guess.
- Never touch, copy or commit anything from `Vendor/groupy`, `Vendor/puntukas_tools2`,
  `Vendor/dephonopy` (private repos).
- Do not commit; leave the changes in the working tree.
