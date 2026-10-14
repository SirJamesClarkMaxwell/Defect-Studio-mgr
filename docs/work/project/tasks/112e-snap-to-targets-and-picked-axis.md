# Task 112e: snap the move onto atoms / bond midpoints / cell points, and an axis picked from two atoms

Plan: `docs/work/project/plans/2026-10-13-structure-building-gizmo.md` (slice 2, first part). User 2026-10-13:
"szybki sposób transformowania elementów np. względem innych atomów". Builds on 112a/112b (modal G/R/S with
lattice fractions, Shift+D).

## Goal

While moving (`G`), the user drops the selection exactly onto something they see: an atom, the midpoint of a bond,
a cell corner, the middle of a cell edge or the centre of a cell face. And while moving or rotating, `V` lets them
click two atoms A, B to use the direction A->B as the axis (move along a bond, rotate about a bond).

## Design (binding)

1. **Snap target mode** - a per-window setting `snapTarget` = `Siatka` (today: increments / lattice fractions) or
   `Cele`, a small toggle in the viewport toolbar next to the existing snap/orientation controls (find them), with a
   tooltip. Ctrl (and the gizmo handle drag with Ctrl) activates the CHOSEN mode, as today for increments.
   `Tab` during the modal operation toggles the mode for that operation only (header shows it).
2. **Targets** (pure, Renderer/Scene - new `ModalSnapTargets.{hpp,cpp}`, no ImGui):
   ```cpp
   enum class SnapTargetKind { Atom, BondMidpoint, CellCorner, CellEdgeMidpoint, CellFaceCentre };
   struct SnapTarget { SnapTargetKind kind; glm::vec3 position; int atomIndex = -1; glm::ivec3 image{0}; };
   // Candidates from the window's atoms (excluding the atoms being moved), bonds between non-moved atoms,
   // and the 8 corners / 12 edge midpoints / 6 face centres of the cell (when the cell box is meaningful).
   [[nodiscard]] std::vector<SnapTarget> CollectSnapTargets(const RendererStructureData &structure,
       std::span<const std::size_t> movedAtoms, bool includeCell);
   // Nearest candidate to the mouse in screen space within `radiusPixels` (projected with the view), ties ->
   // nearer to the camera. nullopt when none.
   [[nodiscard]] std::optional<SnapTarget> PickSnapTarget(std::span<const SnapTarget> targets,
       const ModalTransformView &view, const glm::vec2 &mouse, float radiusPixels);
   // Translation that puts `anchor` on `target`, then constrained: Axis -> projection onto the axis line through
   // anchor, Plane -> projection onto the plane through anchor, None -> exact.
   [[nodiscard]] glm::vec3 TranslationOntoTarget(const glm::vec3 &anchor, const glm::vec3 &target,
       const TransformConstraint &constraint, const TransformBases &bases);
   ```
   Use the existing `RendererStructureData`/`RendererAtomData` positions; bonds from the window's bond list.
   Periodic images: only what is in the window's atom list (no synthetic images in this task; note it).
3. **Viewport glue** (`ViewportModalTransform.cpp`): in Translate with snapping active and mode `Cele`: pick the
   target under the mouse each frame (radius ~12 px), override the delta with `TranslationOntoTarget(anchor, …)`,
   draw a highlight ring on the target and a dashed line anchor->target, header text
   `→ atom S12` / `→ środek wiązania Cu3–S7` / `→ narożnik komórki` / `→ środek krawędzi` / `→ środek ściany`;
   when the constraint leaves a residual distance, show `(odległość od celu 0.37 Å)`. No target -> normal behaviour.
   Collect targets ONCE at operation start (not per frame).
4. **Picked axis `V`** (Translate and Rotate): `V` enters picking: next left click on an atom = A, the second = B
   (Esc in picking cancels picking only, not the operation; picking ignores the move). Then the constraint is an
   Axis along normalize(B - A): add `TransformOrientation::Picked` with `TransformBases::picked` (a mat3 whose
   column 0 is the direction, completed to an orthonormal basis), resolved like the others; the header shows
   `oś A→B (Cu3→S7)`; `X/Y/Z` still switch back to normal constraints. Numeric entry along the picked axis is in Å
   (fractions still parse). Rotation about the picked axis uses the current pivot (Cursor3D lets the user put it
   on A first).
   Pure parts (basis completion, header) unit-tested in `ModalTransformTests`.
5. Hints line under the header gains `Tab cele/siatka · V oś z dwóch atomów`.

## Files to create or change

- new `src/Renderer/Scene/ModalSnapTargets.{hpp,cpp}`, `src/Renderer/Scene/ModalTransform.{hpp,cpp}` (Picked orientation)
- `src/Presentation/Panels/ViewportModalTransform.cpp`, the viewport toolbar file holding the snap controls,
  `src/Renderer/RendererWindowState.hpp` (snapTarget setting + picking state)
- persistence of `snapTarget` only if the other viewport transform settings are persisted in the same place (follow
  `TransformTranslateSnap`; otherwise per-session is fine - say which)
- tests: new `tests/Renderer/ModalSnapTargetsTests.cpp`, additions to `tests/Renderer/ModalTransformTests.cpp`

## Files that must NOT be touched

- Phonon files, `src/IO/Phonon*`, `src/IO/AnalysisDatasetIO*`, `src/IO/DataTableIO*`, plot files (other tasks)
- `CellPanel*`, `CellEditing*` (done)
- `Vendor/`, `install/`, `build/`; `docs/` except `docs/work/project/reports/task-112e-snap-targets.md`

## Acceptance criteria (GoogleTests)

1. `CollectSnapTargets` on a 2-atom structure with one bond and a cubic cell: 2 atoms (minus moved), 1 midpoint
   (only when both ends are not moved), 8 + 12 + 6 cell points; moved atoms excluded.
2. `PickSnapTarget` with the top orthographic view of `ModalTransformTests`: the nearest projected target within the
   radius wins, none outside it.
3. `TranslationOntoTarget`: None exact; Axis along x from anchor (0,0,0) to target (1,2,0) -> (1,0,0); Plane z -> (1,2,0).
4. Picked basis: column 0 = normalized B-A, orthonormal, right-handed; `CycleConstraint`/`ResolveBasis` handle
   `Picked`; header contains `A→B`.
5. Existing tests unchanged and green. Report: files, tests, persistence choice, periodic-image limitation.

## Constraints

- Renderer math stays ImGui-free; Presentation only translates input. Undo unchanged (one commit per operation).
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
