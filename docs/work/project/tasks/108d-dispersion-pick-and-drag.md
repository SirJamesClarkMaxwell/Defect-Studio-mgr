# Task 108d: pick q and branch on the dispersion plot, drag along the path

Plan: `docs/work/project/plans/2026-10-14-vibration-animation.md` (slice 108d). Builds on 108c (`qpoints.tsv`,
`PhononModeJob` with newest-request-wins, per-case `bulkQ`/`bulkBand`/`bulkSupercell`, source `bulk_q`, bulk
supercell window). No Python changes.

## Goal

In `Fonony: wykresy` → Dyspersja the user clicks a band: that q and branch become the case's bulk mode, the source
switches to `mod bulku w q` and the mode is computed. A vertical line marks the picked q; dragging it moves along the
current continuous piece of the path (never jumps across a break) and recomputes on release. The bulk window keeps
animating with the new field.

## Design (binding)

1. **Pure helper** (Presentation, no ImGui; e.g. in `PhononPlotData.{hpp,cpp}` next to `BuildDispersionPlotData`):
   `std::optional<DispersionPick> PickDispersion(const PhononDispersion &, double distance, double frequencyMeV,
   double distancePerPixel, double meVPerPixel)` -> nearest (row, band) in screen-scaled distance among rows with a
   q (requires `qpoints` non-empty; nullopt otherwise or when farther than ~12 px). `DispersionPick` = row, band,
   segment, distance, frequencyMeV, q. And `std::size_t SnapToSegment(const PhononDispersion &, int segment,
   double distance)` -> nearest row of THAT segment (clamped to its first/last row). At a break two rows share a
   distance; the segment decides which one.
2. **Plot** (`PhononModesPanelDispersionPlot.cpp`): left click on the plot area (not on a drag tool, not a pan
   drag: press and release within a few pixels) -> `PickDispersion`; on a pick: set the case's `bulkQ`, `bulkBand`,
   store the picked row/segment in the session (runtime, not persisted), set `animationSource = bulk_q`, submit the
   mode job. Draw: marker at the picked (distance, ω) and `ImPlot::DragLineX` at the picked distance. While held:
   snap to `SnapToSegment(segment, x)`, the marker follows the same branch index on the snapped row, a tooltip shows
   q and ℏω; no job while held. On release (held went true -> false) submit ONE job for the snapped row. Without
   `qpoints` (dispersion computed before 108c): no picking, the plot shows „Przelicz dyspersję, aby wybierać q na
   wykresie.”. After a project load / case switch the line is drawn at the row whose q equals the case's `bulkQ`
   (tolerance 1e-6), if any; otherwise no line.
3. **Supercell while exploring**: if a bulk window is bound to the session and the case's `bulkSupercell` is `auto`,
   a pick/drag request passes the bound window's N×M×K explicitly (so the window keeps matching: non-commensurate q
   becomes a fragment with the 108c warning and the „Użyj N×M×K” hint). Without a bound bulk window `auto` stays
   `auto`. Pressing „Użyj N×M×K” (108c) is the only way to change the window's supercell.
4. **Field swap while animating**: when a new bulk-mode result arrives and the bound bulk window matches it (same
   symbols per atom), keep the animation running with the new field (no stop, rest positions unchanged); if it does
   not match, stop the animation (rest positions restored) and show the existing „Otwórz superkomórkę bulku”.
5. **Newest wins**: a new submission cancels the pending mode job (108c); a result of a cancelled/older request is
   never applied (check a request id, not only cancellation).
6. **Output directories** (follow-up from the 108c review): 108c publishes every request into a new
   `bulk_mode_<uuid>` directory, which piles up while dragging. Name the output deterministically from the request:
   `bulk_mode_q<qx>_<qy>_<qz>_b<band>_<supercell>` (q rounded to 6 decimals, `-` -> `m`, `.` -> `p`; supercell =
   the requested text, `auto` or `NxMxK`) under the same
   parent as today, so repeating a request reuses (overwrites) its directory and different requests never overwrite
   each other (a stale cancelled run can only rewrite the same request's content). Keep the request-id check.

## Files to create or change

- `src/Presentation/Panels/PhononPlotData.{hpp,cpp}` (pure helpers)
- `src/Presentation/Panels/PhononPanelSessionBulkMode.cpp` (output directory naming, item 6)
- `src/Presentation/Panels/PhononModesPanelDispersionPlot.cpp`
- `src/Presentation/Panels/PhononPanelSession.hpp` + the 108c bulk-mode session file (request id, picked row,
  submit-from-pick, field swap)
- tests: `tests/Presentation/Panels/PhononPlotDataTests.cpp` (search the existing plot-data tests and extend them),
  `tests/Presentation/Panels/PhononViewsTests.cpp`

## Files that must NOT be touched

- all Python scripts, IO, Domain, renderer, structure-building files, `Vendor/`, `install/`, `build/`; `docs/`
  except `docs/work/project/reports/task-108d-dispersion-pick.md`

## Acceptance criteria

1. Helpers: pick returns the nearest branch in screen units (anisotropic scales tested), nullopt beyond the
   threshold and without qpoints; snapping stays inside the starting segment at a break with duplicated distances.
2. Session: pick -> source `bulk_q`, case `bulkQ`/`bulkBand` set, one job submitted; two quick requests -> only the
   newest result applied; with a bound bulk window and `auto`, the request carries the window's N×M×K; a matching new
   field keeps `IsAnimating()` true and the rest positions unchanged; a non-matching one stops and restores.
3. Output directory: the same request twice -> the same directory; different q/band/supercell -> different ones.
4. Existing tests pass. Report: files, tests, anything not done; say how the click vs pan distinction behaves.

## Constraints

- No Python, IO or Domain changes; pure helpers testable without ImGui.
- STYLE RULE: keep existing formatting of edited files (no blank-line removal, no joined ifs, no brace changes).
- Regenerate projects after adding files: PowerShell `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
- PowerShell built-ins only. Do NOT run MSBuild; I build and test. Do not commit.
