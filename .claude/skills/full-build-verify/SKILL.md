---
name: full-build-verify
description: "Use before merging any branch to main in this repo, or whenever the user asks to verify/validate a change with a full build. Regenerates premake projects, builds DefectStudio.exe and DefectStudioTests.exe in both Debug and Release, and runs the full GoogleTest suite in both configs. Enforces the project rule in docs/work/project/TODO.md ('Zasady pracy': merge to main only after a full Debug + Release build)."
---

# full-build-verify

Runs this repo's required pre-merge verification: regenerate premake projects, build both
targets in both configurations, run the test suite in both. This is the exact sequence used
manually throughout 2026-08-28 session work (Phases 4-6 of the atoms-displacement feature) -
packaged here so it doesn't need to be re-typed by hand every time.

MSBuild path on this machine (VS 2026/"18", not the vs2022 folder name premake still uses for
the generator output - that's just premake's action name):
`C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`

## Steps

1. **Regenerate projects** (always - cheap, and required if any new .cpp/.hpp was added since the
   last generation; premake globs sources at generation time, not build time):
   ```
   cmd //c "scripts\Windows\GenerateProjects.bat"
   ```

2. **Check nothing has the exe locked** (a running DefectStudio.exe blocks the linker with
   LNK1168 - this is the most common failure mode, not a real compile error):
   ```
   tasklist //FI "IMAGENAME eq DefectStudio.exe"
   ```
   If it's running, ask the user to close it before continuing (never kill it unilaterally).

3. **Build both targets, both configs** (run DefectStudioTests first - it's a separate exe, so it
   still validates compile correctness even if DefectStudio.exe is locked):
   ```
   "/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" build/generated/vs2022/DefectStudioTests.vcxproj //p:Configuration=Debug   //p:Platform=x64 //m //v:minimal
   "/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" build/generated/vs2022/DefectStudio.vcxproj      //p:Configuration=Debug   //p:Platform=x64 //m //v:minimal
   "/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" build/generated/vs2022/DefectStudioTests.vcxproj //p:Configuration=Release //p:Platform=x64 //m //v:minimal
   "/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" build/generated/vs2022/DefectStudio.vcxproj      //p:Configuration=Release //p:Platform=x64 //m //v:minimal
   ```
   Each must end with `<Project>.vcxproj -> ...exe` and no `error` lines. Look at the tail of
   output only - the full MSBuild log is long and mostly noise.

4. **Run the test suite, both configs**:
   ```
   "./build/bin/Debug-windows-x86_64/DefectStudioTests/DefectStudioTests.exe"
   "./build/bin/Release-windows-x86_64/DefectStudioTests/DefectStudioTests.exe"
   ```
   Expect `[  PASSED  ]` for every test except the two known, pre-existing skips
   (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
   `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable` - no nanobind/embedded
   Python module in this build config, `DS_PYTHON_CAPI_AVAILABLE=0`). Any other failure or a
   changed skip count is a real regression - stop and report it, don't merge.

## Reporting

State pass/fail per step, total tests passed/skipped in each config, and whether the two counts
match between Debug and Release (they should). If everything is green, say so plainly - don't
re-run a third time "to be sure".
