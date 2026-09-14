# Task 25b: Blender-style modal G/R/S core + atoms on it

## Goal
G/R/S pressed over the viewport with atoms selected starts a Blender-style modal operation that
follows the mouse. X/Y/Z (Shift = plane) cycle constraints Global/Lattice/None, Ctrl snaps
(Ctrl+Shift fine), typed numbers set the exact value, LMB/Enter confirms (one undo step), Esc/RMB
cancels. The math lives in a pure, unit-tested core (`Renderer/Scene/ModalTransform`); the atom
gizmo only feeds input into it. Labels and arrows move onto the core in 25c.

## Contract (already written - do not change)
- `src/Renderer/Scene/ModalTransform.hpp` - all semantics documented in comments.
- `tests/Renderer/ModalTransformTests.cpp` - currently fails only at link (15 LNK2019).

## Files to create or change
- CREATE `src/Renderer/Scene/ModalTransform.cpp` (split into a second .cpp if it passes ~500 lines).
- `src/Events/RendererEvents.hpp` (~line 228): add `bool startModal = false;` to
  `RendererEvents::Viewport::GizmoOperationRequested`.
- `src/Renderer/Commands/RendererViewportCommands.cpp` (`CreateRendererGizmoOperationCommand`, ~line 235):
  keybinding path publishes with `startModal = true`. Toolbar (ViewportVerticalToolbar.cpp:139) stays false - do not edit it.
- `src/Renderer/RendererLayer.cpp` (`onGizmoOperationRequested`, ~line 1880): set op; if `startModal`,
  set `windowState->modalTransformStartRequested = true`.
- `src/Renderer/RendererWindowState.hpp`: add
  - `bool modalTransformStartRequested = false;`
  - `std::optional<ModalTransformSession> modalTransform;` + `std::vector<glm::vec3> modalTransformStartPositions;`
    (+ the atom indices being moved)
  - `TransformOrientation transformOrientation = TransformOrientation::Global;`
  - `TransformPivotMode transformPivotMode = TransformPivotMode::Median;`
  - `TransformSnapSteps transformSnapSteps;` (UI for the last three comes in 25c)
  - remove `fallbackModalDrag`, `fallbackNumericInput`, `fallbackAxisLockOverride` and any fields
    that become unused.
- `src/Presentation/Panels/ViewportGizmo.cpp` (+ `ViewportSelection.hpp` declarations if needed):
  - Start: request flag set and atoms selected -> `BeginModalTransform` with pivot =
    `ComputeTransformPivot(windowState->transformPivotMode, selected positions, cursor3D if placed)`,
    `bases.lattice = structure.lattice` (no local frame for atoms), snapshot start positions.
    Clear the request flag every frame whether or not a session started (no selection -> G/R/S only switched mode).
  - During session, each frame:
    - X/Y/Z via `IsUnmodifiedModalAxisKeyPressed` (Shift = plane) -> `CycleConstraint`.
    - digits, '-', '.' -> `AppendNumericChar`; Backspace -> `EraseNumericChar`.
      Prefer the project's KeyCode/Input layer over raw ImGuiKey where it covers the key.
    - snap = `SnapModeFromModifiers(ctrl, shift)` - Shift alone must not snap; Shift+X is a plane key press, not a snap.
    - delta = `EvaluateModalTransform(...)`, positions = `ApplyTransformDelta(delta, start_i, pivot)`
      (IndividualOrigins: pivot = start_i) written to the preview.
    - LMB or Enter confirms -> existing `renderer.gizmo.commit_transform` command with
      `GizmoTransformPayload` (skip when `structure.domainStructureId` is empty, as today).
    - Esc or RMB cancels -> restore start positions, no commit.
    - `ImGui::GetIO().WantCaptureKeyboard = true` while active (blocks app keybindings like Ctrl+Z).
    - Draw the constraint in the foreground draw list: axis = long line through pivot along the
      basis column (red/green/blue by column); plane = the two in-plane lines. Draw the header from
      `FormatModalTransformHeader` near the top of the viewport image.
  - Remove the bare X/Y/Z modal start for atoms (outside a session X/Y/Z do nothing for atoms).
  - Clicking-and-dragging an arrow handle becomes a session with an Axis constraint (Global) that confirms on mouse release.
    Trackball rotate click-drag stays as it is.
  - Target: ViewportGizmo.cpp <= ~500 lines (move helpers to a new Presentation .cpp if needed).

## Files that must NOT be touched
- `src/Renderer/Scene/ModalTransform.hpp`, `tests/Renderer/ModalTransformTests.cpp` (contract).
- `ViewportLabelGizmo.cpp`, `ViewportSceneArrowGizmo.cpp`, label/arrow/pin interaction files (25c).
- `ViewportVerticalToolbar.cpp`, Settings panels, persistence (SceneObjects*), UndoStack, keybindings.yaml.

## Acceptance criteria
1. Release `DefectStudioTests` green: all pass, exactly 2 known skips (ConPty, nanobind).
2. Release `DefectStudio` app builds.
3. `grep -rn "fallbackModalDrag\|fallbackNumericInput\|fallbackAxisLockOverride" src` is empty.
4. `git diff -- src/Renderer/Scene/ModalTransform.hpp tests/Renderer/ModalTransformTests.cpp` is empty.
5. Manual (user): G follows mouse; X locks axis, X again -> None (atoms have no Local); Shift+Z = XY plane;
   Ctrl snaps 0.1 A; `G 2 Enter` moves exactly 2 A along X; Esc restores; one Ctrl+Z undoes; `R Z 90 Enter`;
   `S 2 Enter`; header visible; G with only labels selected just switches mode.

## Constraints
- Layer boundaries (CLAUDE.md): Renderer must not include Presentation; ModalTransform.cpp includes no ImGui.
- `#include "Core/dspch.hpp"` first in every .cpp. No exceptions in render paths.
- Known ceiling: labels/arrows keep their bare X/Y/Z start until 25c.

## Round 2 (user test feedback)

### A. Bond lengths after moving atoms

Already completed in commit `84762c5`. Do not touch `RendererAtomEditCommands.*` or
`RendererCommandRegistration.cpp`.

### B. Clickable orientation triad (VESTA-like)

- Draw a small red/green/blue orientation triad in the bottom-left of every viewport image that
  runs `RunViewportGizmoChain`, and make it the first handler so clicks never fall through to
  selection.
- Show `windowState.transformOrientation`: lattice a/b/c vectors or global x/y/z axes, projected
  with camera rotation only at a fixed, font-scaled length, with axis labels and a Lattice/Global
  caption.
- On hover, show `Transform orientation: <name> - click to switch`; clicking toggles Global and
  Lattice. Ignore clicks while a modal transform is active.
- Make atom gizmo arrows follow the normalized columns of `ResolveBasis` using the structure
  lattice, and start handle drags with an Axis constraint in the current transform orientation.
- Keep the triad in its own Presentation source files if existing gizmo files would exceed about
  500 lines. Regenerate projects after adding files and verify ImGuizmo remains at `3bc79f2`.

Keep the modal core contract files and the parallel-branch Presentation/keybinding files listed in
the task request untouched. Build Release `DefectStudioTests` once; if MSB4018, FileTracker, or
access denied indicates sandbox interference, report `build blocked by sandbox` and stop building.
