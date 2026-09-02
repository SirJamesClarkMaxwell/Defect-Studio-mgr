# Build and test

Run from the repository root.

Regenerate project files after adding or removing any `.cpp` or `.hpp` under `src/` or `tests/`:

```powershell
scripts\Windows\GenerateProjects.bat
```

Premake globs sources during generation, not during build. Build with the Python wrapper:

```powershell
python scripts\python\build.py --config Debug
python scripts\python\build.py --config Release
```

Build a target explicitly when needed:

```powershell
python scripts\python\build.py --config Debug --target DefectStudio
python scripts\python\build.py --config Debug --target DefectStudioTests
```

Run the generated test binary for each configuration:

```powershell
build\bin\Debug-windows-x86_64\DefectStudioTests\DefectStudioTests.exe
build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe
```

The generator is named `vs2022`; this machine's MSBuild is under
`C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`.

`DS_PYTHON_CAPI_AVAILABLE=0` is defined in both project configurations (`premake5.lua:545,731`).
Embedded Python is off, so bridges use subprocesses. Two tests are expected to skip:
`ConPtyProcessTests.RunsCommandAndProducesOutput` and
`BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`.

Before merging to `main`: Debug and Release builds, both application and test binaries, and green tests in both.
