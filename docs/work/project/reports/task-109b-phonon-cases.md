# Task 109b: phonon cases, YAML v2 and separate outputs

Implemented the binding case structs, session API and YAML v2 keys, including the persisted
`nextCaseNumber` high-water mark. The active case's inherited fields remain the source of truth.
Existing public methods and field names remain available.

The main Fonony view now has Przypadki above Zestawy, with activation, editable names, duplication,
removal, dataset counts, cached ZPL/S and session-only computation checkboxes. Dataset rows are grouped
into attached, proposed, collapsed Inne (with owner names), and global dispersion. Comparisons remain
global. Missing summaries and generated CCD points remain visible as attached paths with an explanation
instead of an enabled result/curve control.

Activation restores every case field, stops animation, clears the old window's arrows without closing
it, cancels pending case computations, loads the chosen phonon/CCD results and binds the case's window
when available. Dispersion state and its pending job remain global. Window IDs are owned by a case
for the session, preferred when reactivating it, and cleared on project replacement. Case titles and
legacy title/atom matching support restored windows; a window owned by another case is rejected.

Dataset provenance resolves inputs_relative against the result directory, with input_paths fallback
per key. Proposals require matching GS and ES and exclude every attached dataset. Creating cases groups
unattached phonon/CCD datasets by that pair, copies the requested provenance and phonon summary inputs,
uses the common label prefix, and attaches the variants. Repeating the operation creates nothing.

All three case computation paths use CaseOutputDirectory: phonons (including embedding), CCD analysis,
and CCD input generation. Outputs avoid another case's attachments and conflicting known provenance,
using the specified ASCII slug and numbered suffixes. Existing output overrides win. Submitted
computations attach their output before starting, reserving the directory while work is pending;
completion refreshes that attachment. Generated CCD-point directories are attached even before an analysis
summary exists, so a second case does not choose the same directory. Collision results are cached and
invalidated by ownership changes, dataset refreshes and project replacement.

YAML v1 migrates to one case when case data exists, with label / GS folder / Przypadek 1 naming and
result/CCD attachments. An empty project stays at zero cases. The v2 writer uses the active base fields,
keeps global inputs/view/comparisons/styles at the top level and preserves nextCaseNumber even when all
cases are removed. The existing flat parser and emitter supply validation and relative-path rules;
case-schema handling is in a separate file.

## Files changed

Existing IO:

- `src/IO/AnalysisDatasetIO.hpp`
- `src/IO/AnalysisDatasetIO.cpp`
- `src/IO/PhononPanelSettingsIO.hpp`
- `src/IO/PhononPanelSettingsIO.cpp`

New IO:

- `src/IO/PhononPanelSettingsIOCases.hpp`
- `src/IO/PhononPanelSettingsIOCases.cpp`

Existing session:

- `src/Presentation/Panels/PhononPanelSession.hpp`
- `src/Presentation/Panels/PhononPanelSession.cpp`
- `src/Presentation/Panels/PhononPanelSessionState.cpp`
- `src/Presentation/Panels/PhononPanelSessionLifecycle.cpp`
- `src/Presentation/Panels/PhononPanelSessionWindow.cpp`
- `src/Presentation/Panels/PhononPanelSessionJobs.cpp`
- `src/Presentation/Panels/PhononPanelSessionCcdJobs.cpp`
- `src/Presentation/Panels/PhononPanelSessionDatasets.cpp`

New session:

- `src/Presentation/Panels/PhononPanelSessionCases.cpp`
- `src/Presentation/Panels/PhononPanelSessionCaseDatasets.cpp`
- `src/Presentation/Panels/PhononPanelSessionCaseOutputs.cpp`

Main-view sections:

- `src/Presentation/Panels/PhononModesPanelCases.cpp` (new)
- `src/Presentation/Panels/PhononModesPanelDatasets.cpp`
- `src/Presentation/Panels/PhononModesPanelInputs.cpp`

Tests and report:

- `tests/IO/AnalysisDatasetIOTests.cpp`
- `tests/IO/PhononPanelSettingsIOTests.cpp`
- `tests/Presentation/Panels/PhononCasesTests.cpp` (new)
- `docs/work/project/reports/task-109b-phonon-cases.md` (this file)

The new source split stays within the task's allowed paths. Existing blank lines, multiline conditions
and brace styles were retained. Concurrent structure/cell edits and pre-existing workspace changes were
left untouched. Generated build project files were produced only by the requested regeneration command.

## Tests added

17 GoogleTests:

- Three settings-IO tests: v1 migration with attachments; two-case v2 roundtrip using the active base and
  retaining globals; zero cases and deriving a missing high-water mark.
- Two dataset-IO tests: relative provenance with per-key fallback/null handling; absolute and missing
  provenance preserving existing fields.
- Twelve PhononCasesTests: every case field swapped and persisted; actual result loading with per-case
  selected modes; removed IDs surviving save/reopen (including removal of every case); duplicate settings
  with cleared output overrides and membership; ownership collisions, suffixes, UTF-8 replacement and
  the 40-character slug limit across all output kinds; existing-summary provenance conflicts; proposals
  and attachment moves; variant grouping and idempotence; window ownership, legacy matching, animation
  restoration and cached inactive results; first-directory creation and deferred saves/runtime checks;
  migration of the previous flat serializer output; the three migration naming fallbacks.

Existing test expectations were retained, with the single approved exception:
`PhononPanelSettingsIOTests.NewerFormatVersionIsRejected` now rejects formatVersion **3** instead of **2**,
because `kFormatVersion` is now 2. Existing session, views and dataset-session test files were not edited.

## Verification and anything not done

- Scoped `git diff --check` passed. All task C++ files are below 500 lines.
- Tree-sitter checked 25 phonon/IO/test C++ files with no new grammar errors. The session header's
  constructor default argument has the same parser limitation in HEAD. This check is not compilation.
- Regenerated projects with
  `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
  Premake succeeded, and both generated projects include the new sources/tests. The wrapper's
  submodule step printed the known Git sh.exe CreateFileMapping / Win32 error 5 sandbox failure.
- **MSBuild, GoogleTests and interactive GUI validation were not run**, as requested. Compilation and
  Debug/Release test execution remain for the user.
- Graphify was queried first; it did not surface the current phonon implementation. **Graphify update
  was not run**: the later explicit instruction to touch only task-allowed files excludes graphify-out.
  The knowledge graph has not been refreshed by this task.
- Context7 was unavailable in this session; API checks used the official
  [yaml-cpp tutorial](https://github.com/jbeder/yaml-cpp/wiki/Tutorial) and
  [Dear ImGui header](https://github.com/ocornut/imgui/blob/master/imgui.h). No Vendor sources were opened.
- No commit was made. Batch execution itself remains task 109c; this task adds only its runtime case
  checkboxes, as specified.
