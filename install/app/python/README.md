# App-local Python Runtime

This directory stores the Python runtime bundled with DefectStudio at runtime.

Expected platform-specific layout:

- `install/app/python/windows/` on Windows
- `install/app/python/linux/` on Linux
- `install/app/python/macos/` on macOS

The runtime is prepared by:

- `python scripts/python/prepare_app_python_runtime.py`

The repository ignores generated runtime binaries by default. Re-run the script when Python dependencies change.
