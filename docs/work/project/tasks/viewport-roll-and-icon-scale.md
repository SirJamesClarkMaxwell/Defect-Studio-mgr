# Viewport Roll and Icon Scale

Design is pre-approved. No approval step - implement end to end, then report.

Worktree branch: `task/viewport-roll-icon-scale` (from `dev`). Read `AGENTS.md`/`CLAUDE.md` for layer rules.

## A. Alt+Q / Alt+E continuous camera roll

Recon facts: plain Q/E run `renderer.roll_left` / `renderer.roll_right`, non-repeatable
(`install/users/default/config/keybindings.yaml:153-164`); Alt+Arrow orbit bindings show the repeatable convention
(same file ~101-128). "Held" = OS key repeat. No Alt+Q/Alt+E binding exists.

- Add repeatable Alt+Q -> `renderer.roll_left` and Alt+E -> `renderer.roll_right`, same context/layer as Q/E. Keep plain Q/E.
- Search the repo for every other copy of the default keymap (e.g. app-level defaults, fixtures, docs listing
  shortcuts) that contains `renderer.roll_left` and keep them consistent.
- If a keymap test/fixture validates the default file, make sure it still passes.

## B. Toolbar icons scale with Ctrl +/- text scale

Recon facts: Ctrl +/- changes `fontScale` applied as `ImGui::GetIO().FontGlobalScale`
(`src/Presentation/EditorLayer.cpp:1046-1089`, `src/Presentation/ImGuiLayer.cpp:441-450`). Icon base sizes are fixed
20/25 px (`src/Renderer/RendererSettings.hpp:38-42`). `src/Presentation/Panels/ViewportToolbars.cpp:85-141,187-203`
and `src/Presentation/Panels/ViewportVerticalToolbar.cpp:24-84` use raw pixel sizes / clamps / spacing.

- Treat configured icon sizes as scale-1 base sizes. Clamp the base size first, THEN multiply icon extents, frame
  padding, spacing and row-height constants by `ImGui::GetIO().FontGlobalScale`.
- One small shared helper for the scaled value is fine; no new settings.
- Structure-creation tabs reuse these helpers and must scale too.

## Must NOT touch

- Anything under `src/Renderer/Scene/ModalTransform*`, `ViewportGizmo.cpp`, `ViewportModalTransform.*`,
  `ViewportInteraction.cpp`, `RendererAtomEditCommands.*`, `RendererPanel.cpp` (another branch edits those in parallel).

## Rules

- `.cpp` <= ~500 lines, `#include "Core/dspch.hpp"` first, no `glm/gtx/*` includes.
- If a `.cpp`/`.hpp` is added: `python -m scripts.python.generate_projects`, then ensure
  `git -C Vendor/ImGuizmo rev-parse --short HEAD` is `3bc79f2`.
- Build: try ONE build `python -m scripts.python.build --config Release --target DefectStudioTests`. If it fails with
  MSB4018 / FileTracker / access denied (sandbox), stop building and say "build blocked by sandbox". Do not commit.

## Final report

- Files changed/created
- Build result or "build blocked by sandbox"
- Files touched outside the lists above
- Anything risky the reviewer should read in full
