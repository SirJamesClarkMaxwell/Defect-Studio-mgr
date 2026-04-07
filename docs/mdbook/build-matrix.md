# Build Matrix

This page documents the canonical build-matrix workflow used for cross-compiler validation.

## Purpose

The matrix validates project generation and build for:

- `msvc` (Windows only)
- `gcc`
- `clang`

Across configurations:

- `Debug`
- `Release`
- `Dist`

## Canonical Entry Points

Use wrapper scripts instead of calling internal Python scripts directly.

Windows:

```powershell
scripts\Windows\TestBuildMatrix.bat --continue-on-error
```

Linux:

```bash
bash ./scripts/Linux/TestBuildMatrix.sh --continue-on-error
```

## Linux Prerequisites

This project currently targets GLFW on Linux through the X11 backend only.
Wayland can be added later if the project needs it, but it would add another backend path and more packages.

On Ubuntu, install the X11/OpenGL development headers required by the vendored GLFW fork:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxxf86vm-dev libgl1-mesa-dev libegl1-mesa-dev
```

For the current Premake pipeline, Linux is configured as X11-only. Wayland packages are not required.

If you prefer a broader desktop-dev bundle, `xorg-dev` also covers many of these headers, but the explicit package list above is the minimal maintainable option.

The vendored Tracy profiler binary, when needed on Windows, lives under:

- `Vendor/Binaries/Tracy/Windows/tracy-profiler.exe`

## VMware Shared Folders

If the repository lives under `/mnt/hgfs/...`, GNU make can emit the warning about files having modification times in the future.
That is a shared-folder timestamp issue, not a Premake issue.

For reliable Linux builds, clone or copy the repository to a normal Linux filesystem path such as `~/src/Defect-Studio`.


## Artifact Isolation

The matrix runner supports:

- `--clean-between-runs`

Wrappers enable this flag by default. The runner removes per-configuration outputs in:

- `build/bin`
- `build/bin-int`

before each build step, reducing cross-run contamination issues (especially `gcc/clang` on Linux).

## Submodule Cleanup

After vendor updates or matrix runs, `git status` in the superproject may still show dirty submodules.
This usually means local files changed only inside a submodule worktree.

To reset submodules to the commit recorded by the superproject:

```powershell
git -C Vendor/GLFW restore --staged --worktree -- .
git -C Vendor/Tracy clean -fd
git -C Vendor/Tracy restore --staged --worktree -- .
```

Then verify both states:

```powershell
git status --short
git -C Vendor/GLFW status --short
git -C Vendor/Tracy status --short
```

## Manual Runner

If needed, run the Python runner directly:

```bash
python3 scripts/python/test_build_matrix.py --clean-between-runs --continue-on-error
```

On Windows:

```powershell
python scripts\python\test_build_matrix.py --clean-between-runs --continue-on-error
```

## Expected Outcome

A successful run prints:

- `Build matrix finished successfully.`

A failing run prints:

- `Build matrix finished with failures.`

and provides per-step status in the summary table.
