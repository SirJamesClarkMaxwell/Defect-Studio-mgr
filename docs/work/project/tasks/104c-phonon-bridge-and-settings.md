# Task 104c: phonon script bridge + job, and the Phonons panel settings file

## Goal
The app can run `scripts/python/examples/phonon_vibronic_analysis.py` (task 104a) off the main
thread with cancellation, getting back the published result directory, and it can save/load every
setting of the future Phonons panel (task 104d) to `phonon_panel.yaml` in the project.

## Files to create or change
- `src/ScientificRuntime/Python/PhononAnalysisBridge.cpp` (new) - `BuildPhononAnalysisArguments`,
  `ParsePhononAnalysisOutput`, `PhononAnalysisBridge::Run`
- `src/ScientificRuntime/Python/PhononAnalysisJob.cpp` (new) - `PhononAnalysisJob`
- `src/IO/PhononPanelSettingsIO.cpp` (new) - `PhononPanelSettingsIO`

## Files that must NOT be touched
- the headers `src/ScientificRuntime/Python/PhononAnalysisBridge.hpp`,
  `src/IO/PhononPanelSettingsIO.hpp` and the tests
  `tests/ScientificRuntime/PhononAnalysisBridgeTests.cpp`, `tests/IO/PhononPanelSettingsIOTests.cpp`
  (the contract - if something is wrong, stop and say so)
- `src/Domain/Vibrations/*`, `src/IO/PhononAnalysisIO.*`, `tests/Domain/PhononModesTests.cpp`,
  `tests/IO/PhononAnalysisIOTests.cpp` - another task (104b) is writing these in parallel
- everything else under `src/`, `tests/`, `scripts/`, `Vendor/`, `premake5.lua`

## Acceptance criteria
1. `DefectStudioTests.exe --gtest_filter=PhononAnalysisBridgeTests.*:PhononPanelSettingsIOTests.*`
   all pass; `ReferenceCaseRunsEndToEnd` runs (not skipped) on this PC.
2. Rest of the suite unaffected.
3. Each `.cpp` under ~500 lines.

## Constraints
- Follow `src/ScientificRuntime/Python/FnvCorrectionBridge.cpp` / `FnvCorrectionJob.cpp` exactly
  for the bridge/job shape: `ResolvePythonExampleScript`, `ScriptRunOptions`,
  `ExtractJsonLineFromOutput`, `MakePythonExecutionError` / `MakePythonUnavailableError`, job throws
  `std::runtime_error` with userMessage + technicalDetails on failure.
- Set `options.requireZeroExitCode = false`: the script exits 1 with `{"ok": false, "error": ...}`
  and that message must reach the user (`python.phonon.script_failed`). If stdout has no JSON line
  at all, include stderr in technicalDetails and use `python.phonon.invalid_json`. If stderr shows
  `ModuleNotFoundError: No module named 'dephonopy'`, return a
  `MakePythonUnavailableError(..., "python.phonon.dephonopy_not_installed")`.
- Wire cancellation: `options.shouldCancel = shouldCancel` in the bridge, and the job passes
  `[&context] { return context.IsCancellationRequested(); }`.
- `PhononPanelSettingsIO` follows `src/IO/FnvCorrectionsIO.cpp` (yaml-cpp, relative/absolute path
  rule, sibling temp file + rename on save, warnings instead of aborting on bad values).
- Regenerate projects after adding files (`DS_TOOLSET=msc-v143`, `scripts\Windows\GenerateProjects.bat`).
  If the sandbox cannot write into `build/`, report it; the reviewer builds.
