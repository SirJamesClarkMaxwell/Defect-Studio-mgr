# Task 108d: dispersion pick and drag

Implemented the binding design on the 108c bulk-mode job and window flow. Existing public APIs and
existing test assertions are retained. No Python, IO, Domain, renderer implementation, Vendor or
install files were edited. No computation or animation starts on project load or case activation.

## Behavior

- `PickDispersion` returns the closest sampled row and branch within 12 screen pixels, using
  independent distance/pixel and meV/pixel scales. It requires q coordinates and rejects invalid
  scales. `SnapToSegment` searches only the starting segment, including at duplicate break distances;
  out-of-range positions clamp to that segment's endpoints. A missing segment returns distance.size().
- A left press and release inside the plot, outside the legend and drag tool, selects q and branch
  when the maximum mouse excursion is at most 4 pixels. Moving farther, including moving away and
  returning to the press point, remains a pan and submits nothing. This uses maximum excursion,
  rather than just comparing the press and release positions. A successful pick selects `bulk_q`,
  updates the active case, and submits one request.
- The selected point has a marker and a draggable vertical line. Held movement previews the same
  branch at rows snapped within the initial segment, with q and frequency in a tooltip. It submits
  nothing while held and submits once on release. The release position is snapped too. Plot clones
  share the selection but only the plot owning the gesture can submit its release.
- Legacy dispersion without qpoints shows the requested Polish recomputation message and cannot be
  picked. The runtime row is recovered from case bulkQ with component-wise tolerance 1e-6 after
  project load or case switching. A q absent from the path has no line.
- Plot exploration with `auto` and a bound bulk window requests that window's explicit dimensions.
  The case keeps `auto`; without a bound window it requests `auto`. Explicit case sizes remain
  explicit. The existing fragment warning and suggested-supercell button remain in use.
- Matching ordered symbols keep the bound window, phase, pause state and captured rest positions
  through a field swap. The next animation frame uses the new field. Mismatching symbols or atom
  count stop animation, restore rest positions and clear the binding, leaving the existing bulk
  opening action available. No geometry or camera rebuild is performed for a compatible swap.
- Every submission replaces/cancels the previous job slot. A monotonic request generation is checked
  before committing a result, independently of the job cancellation flag. Cancellation and project
  replacement invalidate the generation too. Inactive completion updates only its originating case.
- Output names use `bulk_mode_q<qx>_<qy>_<qz>_b<band>_<requested-size>` under the existing parent.
  Coordinates have six decimals, with minus replaced by m and decimal point by p; rounded zero is
  canonicalized to positive zero. Repeated requests reuse the directory. Completed jobs preserve
  requested q/band/size controls instead of adopting their resolved supercell as a new case setting.

## Files changed

- `src/Presentation/Panels/PhononPlotData.hpp`
- `src/Presentation/Panels/PhononPlotData.cpp`
- `src/Presentation/Panels/PhononModesPanelDispersionPlot.cpp`
- `src/Presentation/Panels/PhononPanelSession.hpp`
- `src/Presentation/Panels/PhononPanelSessionBulkMode.cpp`
- `tests/Presentation/Panels/PhononPlotDataTests.cpp`
- `tests/Presentation/Panels/PhononViewsTests.cpp`
- This report.

The shared session header was re-read immediately before its local patches. New private members
are one adjacent block; existing members and public signatures were retained.

## Tests added

Seven GoogleTests, with every previous assertion retained:

- Three pure-helper tests: anisotropic screen scaling and branch selection; the 12-pixel boundary,
  absent qpoints and invalid inputs; segment snapping, endpoint clamping and duplicated break distances.
- Session request test: one job per pick, replacement cancellation, older and cancelled generation
  rejection, newest completion, retained case controls, invalid rows/bands, and inactive completion.
- Deterministic directory test: repeated request identity, six-decimal encoding and signed zero,
  distinct q components, band and requested size.
- Bound-window test: explicit dimensions for `auto`, real queued submission, a compatible field
  committed through the generation gate, continued animation with a changed displacement direction,
  phase retention, exact rest restoration, and symbol mismatch stopping and unbinding.
- Restoration/legacy test: saved case q, case switching, tolerance, no autoplay, q outside the path,
  and old dispersion without q coordinates.

The existing Gate/BlockingJob utility holds mode execution in the request tests. Synthetic published
datasets enter through the production completion gate and real bulk-mode loader, without Python or
force-constant calculations. Interactive mouse gestures still require GUI verification.

## Adaptations and verification limits

- The existing binding gate requires the window's sourcePath to equal the selected dataset POSCAR.
  For a compatible loaded field, the bulk session adopts the new POSCAR sourcePath on the already
  bound window. This keeps that gate and its public API unchanged while retaining the window's actual
  rest geometry, animation and camera. A mismatching result never changes the old window's source.
- ImPlot's plot itself owns an ImGui hovered item. Consequently the click filter checks plot hover,
  legend hover and the drag tool outputs; a blanket IsAnyItemHovered exclusion would suppress valid
  clicks. The existing PlotWidgets internal-header dependency supplies the legend hover state.
- Production translation units remain below 500 lines (PhononPlotData.cpp: 498). The existing views
  test file exceeds that approximate limit after extension: the task permits only these two existing
  test files, so its unrelated tests were not split or moved. Existing formatting was retained.
- Tree-sitter C++ parsing found no new syntax findings in the changed source/test files. The session
  header retains the same constructor default-argument parser limitation as HEAD. This is not a build.
- `git diff --check` passed. Project generation ran with the requested DS_TOOLSET/GenerateProjects.bat
  command and Premake completed. The wrapper's submodule step emitted the known sandbox sh.exe
  CreateFileMapping / Win32 error 5. No MSBuild was run.
- Graphify was queried first and returned insufficient current context. `graphify update .` was not
  run because the explicit task-only write scope excludes graphify-out files.
- Context7 was unavailable. API checks used the installed ImPlot header and official
  [Dear ImGui declarations](https://raw.githubusercontent.com/ocornut/imgui/master/imgui.h) and
  [ImPlot implementation](https://github.com/epezent/implot/blob/master/implot.cpp).

No requested implementation behavior was deferred. C++ compilation, GoogleTest execution and GUI
validation remain for the user's build/test run, as requested. No commit was made.

## Review (Claude, 2026-10-20)

One new session test did not set the case's bulk FC directory, so `BulkModeBlocker` refused the pick and no job
was submitted; the test now sets it (behaviour unchanged). Verified: Debug tests 1815 (1810 passed, 5 expected
skips), Debug app build. GUI not checked.
