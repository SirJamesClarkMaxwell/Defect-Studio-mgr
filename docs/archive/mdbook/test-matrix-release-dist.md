# Test Matrix (Release/Dist)

This matrix validates test builds and execution for all supported compilers using only Release and Dist.

## Purpose

The workflow runs:

- project generation per compiler,
- test target build for Release and Dist,
- test executable launch for each compiler/configuration pair.

## Canonical Entry Points

Windows:

```powershell
scripts\Windows\TestReleaseDistTests.bat --continue-on-error
```

Linux:

```bash
bash ./scripts/Linux/TestReleaseDistTests.sh --continue-on-error
```

## Scope

Compilers:

- msvc (Windows only)
- gcc
- clang

Configurations:

- Release
- Dist

Target:

- DefectStudioTests

## Manual Runner

If needed, run the Python matrix directly:

```powershell
python scripts\python\test_release_dist_tests.py --continue-on-error
```

```bash
python3 scripts/python/test_release_dist_tests.py --continue-on-error
```

## Expected Outcome

Success prints:

- Test matrix finished successfully.

Failure prints:

- Test matrix finished with failures.

and includes a per-step summary with generate, build, and test phases.
