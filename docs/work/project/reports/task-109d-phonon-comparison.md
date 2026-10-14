# Task 109d: phonon comparison

Implemented the PL axis/normalisation controls and the `Porównanie` tab on the
committed task 110 adapters and 109b/109c case/session flow. Read the repository
rules, named workspace plan and reports for 109a, 109b, 109c and 110 first.

PL has global optional `view.plAxis` (`energy` / `zpl_loss`) and
`view.plNormalization` (`none` / `max` / `area`) keys. YAML stays v2; absent or
invalid keys retain the current energy axis and no normalisation. Invalid values
produce the existing settings warnings. New members are appended to the global
settings struct; case settings and existing fields are retained.

The shared PL adapter selects and records the actual curve choice and its ZPL.
The loss axis is `(own ZPL - photon energy) * 1000`, with paired samples reversed
to retain an ascending grid. Max and trapezoidal area use the entire grid before
the existing neighbour-inclusive trimming. Area is measured in the displayed
axis units, so its density unit is eV^-1 or meV^-1. Plot labels, TSV/CSV exports
and task 111's figure snapshots therefore receive the same transformed numbers.
Fallback source labels are appended to legend text without changing the series
ID, legend ID or style key. A curve without its own ZPL is omitted from the loss
axis and its range; an empty aligned plot explains why it cannot be drawn.

Comparison rows group every known attached phonon dataset under its case, active
case first, followed by compared unattached phonon datasets. A missing attached
selected phonon result remains a row with reasons instead of invented zeros.
The loaded phonon dataset in the active case is the single accent row; selecting
a row uses existing case activation and result loading, after table iteration.
Checkboxes use the existing global compared set. An unrecognised missing result
has a disabled checkbox with an explanation.

Summary fields and modes use the session's existing `Spectra` / `PlotModes`
caches; no new cache or per-frame file reads were added. Failed mode upgrades
retain available summary columns. The strongest non-translation mode supplies
its energy, S_k and n_eff. Missing/nonfinite readouts use `-` and a reason in the
UI, and empty numeric cells through DataTableIO in exports. Zero imaginary modes
is shown only when the loaded result actually supplies the modes/summary data.
The ZPL tooltip names the actual source; without a PL curve, summary ZPL choices
use the requested available value followed by the existing fallback order.

`Eksport TSV…` uses the task 110 table export popup, with an Origin header enabled
initially. `Kopiuj` uses the same columns and header setting through DataTableIO.
There are twelve export columns, exactly matching the table, with units in the
Origin units row. The user approved the small PlotWidgets overload needed for
the comparison label/default filename. The old signature and implementation are
unchanged for the existing modes-table caller.

## Effective frequency definition reused

`scripts/python/examples/ccd_analysis.py:254–271`, function `phonon_mode`, formula
at line **268**:

`hbar_omega_eff_meV = sqrt(sum((frequencyMeV * q_k)^2) / sum(q_k^2))`

Translation modes are excluded. As in Python, retained frequencies must be
finite and positive, q_k finite, and the squared displacement sum positive.
Invalid inputs leave an absent readout with a reason. The C++ port has a source
comment at the calculation. The current phonon summary/result has no stored
effective-frequency field. `phonon_vibronic_analysis.py` was also inspected.

## Files changed by task 109d

- `src/IO/PhononPanelSettingsIO.hpp`
- `src/IO/PhononPanelSettingsIO.cpp`
- `src/Presentation/Panels/PhononPlotData.hpp`
- `src/Presentation/Panels/PhononPlotData.cpp`
- `src/Presentation/Panels/PhononModesPanel.hpp` (one method and one export-state member)
- `src/Presentation/Panels/PhononModesPanelPlots.cpp`
- `src/Presentation/Panels/PhononModesPanelComparison.cpp` (new)
- `src/Presentation/Panels/PlotWidgets.hpp` (approved overload)
- `src/Presentation/Panels/PlotWidgets.cpp` (approved overload)
- `tests/IO/PhononPanelSettingsIOTests.cpp`
- `tests/Presentation/Panels/PhononPlotDataTests.cpp`
- `tests/Presentation/Panels/PhononComparisonTests.cpp` (new)
- This report.

No task 109d edits to session files, Domain, ScientificRuntime, Python scripts,
renderer/structure files, Vendor or install. Concurrent task 111's figure members
and its call beside the plot export button were preserved. Shared files were
re-read and patched locally; no whole-file rewrites or unrelated formatting edits
were made. The working-tree diff also contains independent task 111/112f changes.

## Tests added

Twelve GoogleTests:

- Four PL adapter tests: ZPL 1.9/2.5 eV both peak at zero and normalise separately;
  max/area use samples beyond the trimmed range, including the eV/meV Jacobian
  and export values; fallback records/labels computed, spin-purified and formula
  sources while retaining identity/styles; missing-ZPL curves do not widen the
  aligned range. Compared-only operation is also covered.
- Seven comparison tests: grouping, active accent, attached variants and orphan
  filtering; strongest mode and q_k-weighted effective frequency; imaginary and
  invalid/zero/nonfinite displacement handling; missing values and empty numeric
  export cells; summary preservation after failed mode loads, absent ZPL and
  translation-only modes; cached reads surviving changed files until refresh;
  summary ZPL fallback and all export columns/Origin units.
- One settings test covers both normalisation modes, shifted-axis roundtrip,
  missing keys in v1/v2 and invalid-key defaults/warnings.

All 31 existing tests in the two extended test files retain their bodies
byte-for-byte after newline normalisation. No existing expectations were edited.

## Verification and anything not done

- Scoped `git diff --check` passed. Task C++ translation units parsed with the
  installed Tree-sitter C++ grammar. The panel header retains only the existing
  parser limitation at `DrawFolder`'s default argument. This is not compilation.
  All task `.cpp` files are below 500 lines; the largest is the extended settings
  test at 497 lines.
- Regenerated projects with
  `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
  Premake succeeded, and app/tests include the new comparison source; the test
  project includes PhononComparisonTests.cpp. The wrapper's submodule step emitted
  the known sandbox Git `sh.exe` CreateFileMapping / Win32 error 5.
- **MSBuild, GoogleTest execution and interactive GUI validation were not run**,
  as requested. Compilation, Debug/Release test execution and visual checks
  remain for the user's build. No commit was made.
- Graphify was queried first and returned insufficient current phonon context.
  Graphify update was not run because the user's explicit allowed-file scope
  excludes writes to graphify-out.
- Context7 was not callable; ImGui API declarations were checked against the
  official [Dear ImGui header](https://raw.githubusercontent.com/ocornut/imgui/master/imgui.h).
  No Vendor sources were opened for API questions.

All requested implementation and test additions are present. Executable
validation remains unverified.
