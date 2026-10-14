# Task 112g: `Powtórz…` (array), `Odbij…` (mirror) and `Shift+R` repeat last operation

Plan: `docs/work/project/plans/2026-10-13-structure-building-gizmo.md` (slice 3 without pattern painting; row
"Powiel i przekształć" for `Shift+R`, row "Szereg / lustro"). Builds on 112b (`Shift+D`), 112c (replace-structure
command), 112d (`ParseNumericText` for typed fractions).

## Goal

Building a motif once and spreading it without knowing symmetry:
- `Powtórz…`: N copies of the selected atoms, copy k shifted by k * v (v = a fraction of a/b/c, or a Cartesian
  vector in Å, or the vector between two selected atoms - first to last selected).
- `Odbij…`: mirror the selected atoms through a plane (point = 3D cursor, normal = an axis of the current
  orientation: x/y/z or the plane spanned by two cell vectors) or invert through the 3D cursor; `kopiuj` (default
  on) keeps the originals.
- `Shift+R` in the viewport: repeat the last CONFIRMED modal operation (same op, constraint and `TransformDelta`) on
  the current selection; if that operation started with `Shift+D`, duplicate first (so `Shift+D G X 1/4 Enter`,
  then `Shift+R Shift+R` makes a row of atoms). One undo entry per press.

## Design (binding)

1. **Domain, pure** (`src/Domain/Crystal/CellEditing.{hpp,cpp}`, next to `AddSublattice`):
   ```cpp
   // Appends count copies of the atoms at `indices`; copy k (1..count) is shifted by k * shift (Cartesian Å).
   // Copies landing on an existing site (within the AddSublattice tolerance) are still added and reported.
   [[nodiscard]] CrystalStructure RepeatAtoms(const CrystalStructure &structure, std::span<const std::size_t> indices,
       const glm::vec3 &shift, int count, std::vector<std::size_t> &outCoincident);
   // Reflects the atoms at `indices` through the plane (point, normal) - or through `point` when normal is zero
   // (inversion). copy = append the images, else move the originals in place. Coincident images reported.
   [[nodiscard]] CrystalStructure MirrorAtoms(const CrystalStructure &structure, std::span<const std::size_t> indices,
       const glm::vec3 &point, const glm::vec3 &normal, bool copy, std::vector<std::size_t> &outCoincident);
   ```
   No wrapping (wrap stays explicit in the Komórka panel). Fractional coordinates updated with the positions.
2. **UI**: two items in the viewport context menu under a `Przekształć` submenu (`RendererPanelContextMenu.cpp`)
   and the same two buttons in the Komórka panel (`CellPanel.cpp`, a small section `Zaznaczone atomy`), both
   opening one popup per action (new file `src/Presentation/Panels/CellPanelSelectionOps.cpp` holds the popups so
   both callers share them). Popup: inputs with typed fractions (`ParseNumericText`), a live line
   `doda 12 atomów, 2 nakładają się` (preview computed once per input change, not per frame), `Wykonaj` through
   `kReplaceStructureCommandId` (one undo entry), disabled with a reason when nothing is selected or the input does
   not parse. Labels left/above, tooltips on every control.
3. **`Shift+R`** (`ViewportModalTransform.cpp` / `ViewportInteraction.cpp`, keymap entry
   `renderer.gizmo.repeat_last` with chord `Shift+R` in the default keybindings, same route as `Shift+D`): the
   window state keeps the last confirmed `{op, constraint, orientation, TransformDelta, pivot mode, duplicated}`;
   Shift+R applies it to the current selection through the same commit path as a confirmed modal operation (pivot
   recomputed for the current selection, not the old one); nothing stored or empty selection -> no-op with a
   status/log line. Cancelled operations do not overwrite the stored one.

## Files to create or change

- `src/Domain/Crystal/CellEditing.{hpp,cpp}`, new `src/Presentation/Panels/CellPanelSelectionOps.cpp`,
  `src/Presentation/Panels/CellPanel.{hpp,cpp}`, `src/Presentation/Panels/RendererPanelContextMenu.cpp`
- `src/Presentation/Panels/ViewportModalTransform.cpp`, `ViewportInteraction.cpp`, `src/Renderer/RendererWindowState.hpp`,
  `src/Renderer/Commands/RendererCommandRegistration.cpp`, `install/users/default/config/keybindings.yaml` (one entry)
  and the built-in default keymap if there is one in `src/` (search where `renderer.gizmo.mode_rotate` is defined)
- tests: `tests/Domain/Crystal/CellEditingTests.cpp` additions; a repeat-last test next to the 112b duplicate tests
  (`tests/Renderer/RendererAtomDeleteCommandTests.cpp` or a new file) if the commit path is testable headless

## Files that must NOT be touched

- Phonon files, plot files, `src/IO/*` (keybindings file above excepted)
- `Vendor/`, `build/`; `docs/` except `docs/work/project/reports/task-112g-repeat-mirror.md`

## Acceptance criteria (GoogleTests)

1. `RepeatAtoms`: one atom at origin, shift (1,0,0), count 3 -> 4 atoms at x = 0,1,2,3; coincident reported when a
   copy lands on an existing atom.
2. `MirrorAtoms`: plane x = 0.5 (point (0.5,0,0), normal x) maps (0.2,0.3,0) to (0.8,0.3,0); copy keeps the original;
   move does not; inversion through (0.5,0.5,0.5) maps (0,0,0) to (1,1,1); fractional coordinates follow.
3. Repeat last: after a confirmed duplicate+move, repeat creates one more copy shifted by the same delta, one undo
   step removes it (only if testable without ImGui; otherwise say so in the report).
4. Existing tests unchanged and green. Report: files, tests, what was not testable.

## Constraints

- Domain stays UI-free; Presentation builds the new structure through Domain and commits through the command.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
