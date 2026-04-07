# Start

Defect Studio uses a script-first build workflow for reproducible local setup and validation.

## Build Matrix

Use wrapper scripts as the canonical way to run full build validation.

Windows:

```powershell
scripts\Windows\TestBuildMatrix.bat --continue-on-error
```

Linux:

```bash
bash ./scripts/Linux/TestBuildMatrix.sh --continue-on-error
```

Both wrappers run the Python matrix runner with `--clean-between-runs` enabled to avoid artifact collisions between compiler/configuration passes.

See the dedicated page for details: [Build Matrix](build-matrix.md).
