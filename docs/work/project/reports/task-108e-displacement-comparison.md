# Task 108e: displacement comparison

Implemented the five displacement readouts, localisation plot and overlap matrix in
`Fonony: wykresy` → `Porównanie`. Read AGENTS.md, CLAUDE.md, the task, the
2026-10-14 vibration-animation plan and the 108b, 108c, 108d, 109d and 110 reports first.

## Behaviour

- Pure domain functions calculate the mass-weighted periodic displacement centre,
  cumulative localisation curve, minimum carrying-atom count and absolute normalised
  overlap. The centre unwraps around the atom with the largest weight and returns an
  equivalent position in the cell. Cartesian nearest-image searches handle skewed
  cells and positions outside the primary cell. Invalid/zero-weight inputs return
  absent/empty results (or zero carrying atoms).
- The comparison table appends CCD ΔQ, maximum physical displacement, atoms carrying
  the configured fraction, mode coverage and the largest mode share. CCD values use
  each case's selected CCD dataset, including inactive cases. Missing selections and
  old datasets show the specified Polish reasons. A case with only a selected CCD
  dataset also gets a row; selecting that row uses the existing CCD loader.
- Coverage divides the sum and maximum of non-translation q_k² by that phonon
  result's own ΔQ², independently of the CCD norm and mode frequencies. Invalid or
  missing inputs have reasons. Coverage above 100% is retained rather than clipped.
- The new `localisation` snapshot has one line per active or compared case with
  displacement data, deduplicating cases with multiple compared phonon variants.
  It follows the existing S/PL convention of including the active case. Series use
  the case name, CCD directory provenance, stable case IDs and persisted styles.
  Legend editing, TSV/CSV/clipboard export, visible-series filtering and the figure
  button reuse the existing widgets. Axis resets use their own tab discriminator.
- `Podobieństwo ΔR` shows three-decimal overlaps or an em dash with an explanation.
  Tooltips explicitly require the same ordered atoms, matching masses and nonzero
  norms, without atom assignment or rotation. Its separate table export uses empty
  numeric cells for unavailable comparisons. Very wide matrices are drawn in column
  blocks within ImGui's table limit; export retains the complete matrix.
- CCD results and failed reads reuse `m_CcdResults`, with summary/displacement mtimes
  stored in each existing entry. Unchanged data is not reparsed; changed, created or
  removed files invalidate that entry and the plot revision. Dataset refresh still
  clears the existing cache. There is no second displacement cache.

## Files changed

New:

- `src/Domain/Vibrations/DisplacementAnalysis.hpp`
- `src/Domain/Vibrations/DisplacementAnalysis.cpp`
- `src/Presentation/Panels/PhononPlotDataComparison.cpp`
- `tests/Domain/Vibrations/DisplacementAnalysisTests.cpp`
- This report.

Modified:

- `src/Presentation/Panels/PhononPlotData.hpp`
- `src/Presentation/Panels/PhononPlotData.cpp`
- `src/Presentation/Panels/PhononModesPanelComparison.cpp`
- `src/Presentation/Panels/PhononPanelSession.hpp`
- `src/Presentation/Panels/PhononPanelSessionDatasets.cpp`
- `tests/Presentation/Panels/PhononComparisonTests.cpp`

No Python, IO, renderer, structure-building, Vendor or install files were edited.
Generated project files were produced only by the required generator. Existing
unrelated untracked workspace files were left untouched. No commit was made.

## Compatibility adaptations

1. Existing tests explicitly require the one-argument `PhononComparisonColumns(rows)`
   API to return twelve columns and exact legacy TSV/Origin headers. That API retains
   its schema. The added `(rows, carriedFraction)` overload exports all seventeen
   columns and the actual fraction in the carrying-atom header. The UI's save and
   copy actions both use the full overload. No existing assertion was changed.
2. `PhononPlotData.cpp` was already 498 lines, while the old comparison adapters lived
   in the ImGui translation unit. Related comparison functions were moved to the
   new `PhononPlotDataComparison.cpp` beside the new displacement adapters, keeping
   adapters free of ImGui and every task .cpp below 500 lines. The existing plot-data
   file only gains stable localisation legend identity and is now 499 lines.
3. The existing comparison cache does not actually invalidate on mtime: phonon tests
   require cached reads to survive file changes until explicit dataset refresh.
   Those phonon semantics remain intact. Timestamp invalidation was added only to
   the existing CCD cache needed by this task.
4. The existing shared minimum-image helper takes float vectors and searches only
   27 neighbouring cells. The new domain calculation uses double precision and
   inverse-cell row bounds to search all potentially closer images, preserving CCD
   precision and correct distances in skewed cells without editing shared geometry.

## Tests added

Eleven GoogleTests:

- Six domain tests: periodic boundary centre and unwrapped positions; skewed-cell
  nearest images; carrying counts at several fractions; identical, orthogonal,
  partial, sign-reversed and scaled overlaps; count/order/mass incompatibility and
  tolerance; empty, zero, malformed, singular and nonfinite inputs.
- Five comparison/adapter tests: missing and legacy CCD reasons, selected CCD values,
  all seventeen export columns and configured percentage; translation exclusion and
  invalid mode coverage; active/inactive and compared-case localisation, variant
  deduplication, style/legend identity, provenance and exports; overlap values and
  empty incompatible cells; lazy cache retention, displacement/summary timestamp
  invalidation and recovery after a failed displacement read.

All seven existing comparison test bodies were checked against HEAD and remain
unchanged after newline normalisation. Existing plot-data tests were not edited.

## Verification and anything not done

- `git diff --check` passed. Existing CRLF formatting was retained.
- Tree-sitter C++ parsing found no new syntax findings across the ten source/test
  files. The session header retains the same constructor-default-argument parser
  limitation as HEAD. This is a grammar check, not compilation.
- Projects regenerated with
  `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
  Premake completed successfully; both projects register the new production sources
  and the test project registers the new domain tests. The wrapper's submodule step
  emitted the previously reported sandbox sh.exe CreateFileMapping / Win32 error 5.
- Graphify was queried before repository navigation and returned insufficient
  current phonon context. `graphify update .` was not run because the explicit task
  file scope excludes writes to graphify-out.
- Context7 was not callable. The UI reuses existing plot/table APIs; the matrix
  table limit was checked against the official
  [Dear ImGui table implementation](https://raw.githubusercontent.com/ocornut/imgui/master/imgui_tables.cpp)
  and [internal declarations](https://raw.githubusercontent.com/ocornut/imgui/master/imgui_internal.h).
  No Vendor API sources were read.
- MSBuild, C++ test execution and interactive GUI validation were not run, as
  requested. Compilation, executable test results and visual validation remain for
  the user's build/test run. No requested implementation behaviour was deferred.

## Review (Claude, 2026-10-20)

Domain math checked (minimum image with reciprocal-row bound, ΔQ²-weighted centre anchored at the heaviest
contribution, mass-weighted overlap). Verified: Debug and Release, DefectStudio + DefectStudioTests, 1826 tests
each (1821 passed, 5 expected skips); Domain has no IO/UI/Renderer includes. GUI not checked.
