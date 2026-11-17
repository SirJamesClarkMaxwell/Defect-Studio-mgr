# Task 113f1: numeric text tables and experimental spectrum

## Files changed

- `src/IO/DataTableIO.cpp`: implemented `ParseDataTable` beside the existing writer, plus `LoadDataTable` through `TextFileIO`. Delimiter selection follows the header contract; numeric fields use locale-independent `std::from_chars`, decimal commas are converted only outside comma-delimited input, and non-finite/partial numbers are rejected. Preserves empty delimited fields, handles BOM/CRLF, selects names including Origin headers, counts skipped non-empty lines, and leaves output untouched on failure. Existing writer code is unchanged.
- `src/Domain/Vibrations/ExperimentalSpectrum.cpp`: implemented the eV conversion switch, finite-pair filtering, stable energy sorting, duplicate-energy averaging without overflowing a sum, validation after merging, and inclusive-window peak lookup. Failure leaves output untouched; successful intensities are not normalised. No IO/UI dependencies.
- `docs/work/project/reports/task-113f1-text-table.md`: this report.

Headers, tests, and task 113a files were not edited. Source files are 206 and 90 lines respectively.

## Validation

- Ran the requested project regeneration with `DS_TOOLSET=msc-v143`; exit code 0. Confirmed `ExperimentalSpectrum.cpp` is listed in both application and test projects, and `ExperimentalSpectrumTests.cpp` is listed in the test project.
- Regeneration printed a Git `sh.exe` CreateFileMapping error (Win32 error 5) during its submodule step and existing premake deprecation warnings. It continued and generated the projects successfully. Generated, ignored project files were updated by this explicitly requested command.
- `git diff --check -- src/IO/DataTableIO.cpp` passed. Reviewed both sources for trailing whitespace, layer dependencies, line limits, error paths, and the supplied test cases. The writer diff contains additions only.
- No MSBuild or C++ tests were run, as requested. No tests can be reported as passing from this session. The intended coverage is the four existing writer tests, four new parser tests in `DataTableIOTests`, and three `ExperimentalSpectrumTests`.
- Pytest was not run: this task changes no Python code, and its task instructions restrict commands to PowerShell built-ins plus the required regeneration workflow.

## Contract issues and deviations

- The supplied `tests/IO/DataTableIOTests.cpp` contains physical newlines inside ordinary C++ string literals beginning at lines 54, 75, 83, 91, and 115. These are neither raw strings nor escaped continuations and appear to prevent compilation. Left them untouched as required; the caller must correct their literal syntax before building those tests, without changing their expected values.
- Blank lines are not counted in `skippedLines`, following the struct's definition and the explicit expectation of 5 in `ParsesHeaderCommentsAndWhitespace`. This resolves the conflicting sentence in the header that says empty lines are counted after data begins. Whitespace-only lines are likewise ignored; delimiter-only preamble lines remain available for Origin header selection.
- Graphify was queried first but returned unrelated nodes, so scoped source inspection supplied the implementation context. Did not run `graphify update .`: it writes files outside this task's strict three-file edit list and could overlap the concurrent task's graph changes.
- No commits, dependency additions, UI work, or additional axis units.
