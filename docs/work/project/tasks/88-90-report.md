# Tasks 88-90 report - GL warning, FNV saved with the project, hide-volume gizmo (2026-10-08)

Branch chain: `task/87-fnv-panel` -> `task/88-gl-counter-readback` -> `task/89-fnv-persistence` ->
`task/90-hide-volume-gizmo` (holds everything, including the review fixes). Nothing pushed or merged.

## 88 - iso slider no longer floods the log

The isosurface vertex counter was read straight back from a GL_DYNAMIC_COPY SSBO, so the NVIDIA
driver migrated it to host memory on every re-march and logged warning 131186 per slider step. It is
now copied GPU-side into a 4-byte GL_STREAM_READ buffer and read from there. The GL density test run
logged 131186 before the change and none after.

## 89 - FNV corrections are saved (contract by Claude, implementation by Codex)

`fnv_corrections.yaml` next to `manifest.yaml`, one entry per defect LOCPOT: inputs, G = 0
convention, axes in use and, once computed, the profiles and plateau windows (alignments are
recomputed on load). Written on every change, once on mouse release while dragging a window.

Try it: open a project, compute GeV q-1, drag a window edge, close the app, reopen - the result
is there without "Oblicz profile". Compute q+1, then "Zapisane" switches back to q-1. "Nowe" starts
an empty one, "Usuń" removes the shown one. Without a project nothing is written (session only).

## 90 - gizmo for hide volumes

Select a volume in the Scene Outliner: the G/R/S gizmo stands on its centre. Translate moves it
(and unpins it from the atoms, as for planes), rotate turns its axes (keeps the pin while the centre
stays), scale grows the radius / half-sides. Atoms appear and vanish while dragging; Esc and Ctrl+Z
restore. Not done: clicking a volume in the viewport to select it (it is only drawn while selected).

## Review (Codex) - four findings, all fixed

- Cloned FNV windows kept separate copies of the file and could overwrite each other -> shared.
- Any project-root edit reloaded the FNV file and reset the panel -> loads on project change only.
- A Fractional volume turned/scaled in fractional space and showed fractional axes as Local ->
  converted through the lattice (exact in any cubic cell, rotated or not; re-orthonormalised in a
  non-cubic one).

## Tests

Release 1386 passed, 3 skipped (the usual ConPty and two env-gated ones). New: `FnvCorrectionsIOTests`
(6), `SceneTransformHideVolumeTests` (4).
