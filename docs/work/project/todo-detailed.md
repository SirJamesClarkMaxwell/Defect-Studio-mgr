# TODO Detailed – T01 / T03 Expansion

This document expands and reorganizes **T01** and **T03** so they are immediately actionable and consistent with the current DefectsStudio direction: a **single desktop executable**, a **modular domain monolith**, and Python used as a **Scientific Runtime** rather than as end-user scripting in MVP.

---

## General Intent

The setup/tooling layer should make a clean machine able to:

1. update the repository if requested,
2. detect or install missing tools,
3. prepare the Python environment,
4. generate project files,
5. build the selected configuration,
6. optionally open the IDE,
7. optionally run the application.

The user-facing entrypoints should be:

- `Setup.bat` / `Setup.sh` – full bootstrap and optional build/run
- `GenerateProjects.bat` / `GenerateProjects.sh` – regenerate only
- `Build.bat` / `Build.sh` – compile only
- `Run.bat` / `Run.sh` – run only
- `BuildAndRun.bat` / `BuildAndRun.sh` – compile and run
- `ExportPythonEnv.bat` / `ExportPythonEnv.sh` – freeze/export Python environment

The **canonical implementation** should live in Python, with shell/batch files acting only as thin wrappers.

---

## Command and Script Strategy

### Canonical Python scripts

Recommended canonical scripts under `scripts/`:

- `scripts/setup.py`
- `scripts/generate_projects.py`
- `scripts/build.py`
- `scripts/run.py`
- `scripts/build_and_run.py`
- `scripts/export_python_env.py`
- `scripts/doctor.py`
- `scripts/common/` for shared helpers

### Wrapper scripts

Recommended wrappers at repository root or `scripts/`:

- `Setup.bat` / `Setup.sh`
- `GenerateProjects.bat` / `GenerateProjects.sh`
- `Build.bat` / `Build.sh`
- `Run.bat` / `Run.sh`
- `BuildAndRun.bat` / `BuildAndRun.sh`
- `ExportPythonEnv.bat` / `ExportPythonEnv.sh`

Each wrapper should only:

1. locate Python,
2. forward arguments,
3. call the canonical Python script,
4. propagate exit code.

### Interaction model

Default behavior should be **interactive with sensible defaults**.

Recommended modes:

- default: interactive prompts, default answers preselected,
- `--defaults`: non-interactive, use predefined defaults,
- `--yes`: auto-confirm prompts,
- `--ci`: deterministic non-interactive mode for CI/local verification,
- `--verbose`: more diagnostics,
- `--dry-run`: show intended actions without changing anything.

### Default path strategy

The setup flow should first search for existing tools, then offer default managed locations.

Recommended managed locations:

- repo-local virtual environment: `.venv/`
- repo-local build output: `build/`
- repo-local generated files: `build/generated/` or `buildfiles/`
- repo-local managed tools: `tools/`
- repo-local logs: `logs/`

Recommended managed tool layout:

- `tools/premake/`
- `tools/mdbook/`
- `tools/uv/`
- optional `tools/python/` only if no suitable Python is present

The script should always prefer:

1. existing compatible system installation,
2. existing managed local installation,
3. guided install into default managed location,
4. custom path provided by flag.

---

## Revised T01 – Platform Setup New Code Base (`task/01-platform-setup`)

**Goal:** establish the bootstrap, build, and app-shell baseline for DefectsStudio on Windows first, without over-downloading future dependencies.

### T01.1 – Repository and directory baseline

- [ ] Create and validate repository structure:
- `src/App/`
- `src/Core/`
- `src/Domain/`
- `src/Presentation/`
- `src/IO/`
- `src/Storage/`
- `src/ScientificRuntime/`
- `src/Debug/`
- `Vendor/`
- `Assets/`
- `Docs/`
- `Scripts/`
- `Tests/`
- `Tools/` 
- [ ] Define what is source-owned vs generated:
- tracked source files,
- tracked vendor metadata,
- generated project files,
- build outputs,
- caches,
- downloaded binaries.
- [ ] Add `.gitignore` entries for:
- `.venv/`
- `tools/`
- build outputs,
- IDE caches,
- local logs,
- exported requirements snapshots.
- [ ] Keep a single authoritative place that describes folder purpose, for example `docs/project-layout.md`.

### T01.2 – Premake workspace and build configurations

- [ ] Create `premake5.lua` workspace for Visual Studio 2022.
- [ ] Enable `C++23` and define `Debug`, `Release`, `Dist`
- [ ] Enable mutlithread code compilation
- [ ] Add optional internal diagnostics switch for Release-like builds.
- [ ] Separate output folders by:
- action/generator,
- compiler/toolset,
- architecture,
- configuration.
- [ ] Add central warning policy and runtime library policy.
- [ ] Add per-project output naming conventions.
- [ ] Ensure future Linux generator parity is possible without rewriting the workspace.

### T01.3 – Entry point and GUI application shell

- [ ] Configure the application target as a **GUI app**, not a console app, for normal desktop usage.
- [ ] On Windows, use the proper GUI subsystem so the application does not spawn a startup console window.
- [ ] Keep console-based tooling separate from the GUI application executable.
- [ ] Decide whether Debug uses the same GUI subsystem or a separate debug launcher.
- [ ] Add `VERSIONINFO` and `.rc` resource integration early.
- [ ] Add application icon resources for Debug/Release/Dist.

### T01.4 – Minimal runtime foundation

- [ ] Add PCH per module with `.hpp` convention.
- [ ] Add spdlog as submodule (ask for github repository)
- [ ] Add `spdlog` bootstrap with:
- log levels,
- source location metadata,
- console sink for tooling,
- file sink for application runtime.
- [ ] Add startup diagnostics banner:
- app version,
- build config,
- git commit if available,
- compiler/toolset,
- capability snapshot.
- [ ] Add a minimal `Application` / `AppShell` bootstrap compatible with the architecture.

### T01.5 – Vendor and dependency management baseline

- [ ] Define one dependency policy file that says for each dependency whether it is:
- header-only vendored,
- submodule/vendor source,
- precompiled tool/binary,
- Python package in Scientific Runtime,
- deferred.
- [ ] Keep dependency acquisition automated where realistic.
- [ ] Avoid globally mutating the user machine unless explicitly requested.
- [ ] Prefer repo-local managed tools for setup utilities.

### T01.6 – Setup orchestration

- [ ] Implement `scripts/setup.py` as the canonical bootstrap entrypoint.
- [ ] Make `Setup.bat` and `Setup.sh` thin wrappers around it.
- [ ] Support the following phases in one script:
- repository update,
- tool discovery,
- optional tool install,
- Python env creation/sync,
- vendor/dependency check,
- premake generation,
- build,
- IDE open,
- run.
- [ ] Each phase should be callable independently through flags.
- [ ] The script should explain what it is doing before changing the system.
- [ ] Missing dependency handling should be interactive:
- found,
- not found but installable automatically,
- not found and user must provide a path,
- user skipped.
- [ ] Support re-entry: rerunning setup on an already prepared machine should be safe.

### T01.7 – Tool discovery and installation policy

- [ ] Detect and validate:
- Git,
- Python,
- uv,
- Premake5,
- Visual Studio 2022 / MSVC,
- optional VS Code,
- mdBook.
- [ ] Validate version compatibility where needed.
- [ ] Provide a clear status summary before execution starts.
- [ ] Allow the user to choose:
- use system installation,
- use managed local installation,
- provide custom path,
- skip optional tool.
- [ ] Persist discovered paths in a local machine-specific config file, for example `.local/toolchain.json`.

### T01.8 – Python environment bootstrap

- [ ] Use `uv` as the canonical Python environment manager.
- [ ] Create repo-local `.venv/`.
- [ ] Define Python dependencies in `pyproject.toml` with groups, instead of treating `requirements.txt` as the primary source of truth.
- [ ] Add at least these dependency groups:
- `dev`
- `scientific-core`
- `scientific-extended`
- `docs`
- [ ] Setup should be able to install only selected groups.
- [ ] Add a smoke test that imports required packages and reports versions.
- [ ] Add runtime capability checks so the application can later report missing Python capabilities cleanly.

### T01.9 – Python environment export / freeze

- [ ] Add `scripts/export_python_env.py`.
- [ ] Its purpose is to:
- refresh the lock state,
- export a human-readable snapshot of currently installed Python packages,
- optionally export group-specific snapshots.
- [ ] Prefer `uv.lock` as the canonical lock artifact.
- [ ] Optionally export `requirements.txt`-style snapshots for inspection, debugging, or interoperability.
- [ ] Recommended outputs:
- `uv.lock`
- `scripts/requirements/requirements-dev.txt`
- `scripts/requirements/requirements-scientific-core.txt`
- `scripts/requirements/requirements-scientific-extended.txt`
- [ ] The export script should clearly state whether it is:
- updating lock state,
- only exporting,
- exporting exact current environment,
- exporting declared project groups.

### T01.10 – Project generation scripts

- [ ] Add `scripts/generate_projects.py`.
- [ ] It should support:
- Visual Studio 2022,
- future `gmake2`,
- selected architecture,
- selected compiler/toolset,
- cleaning previous generated files if requested.
- [ ] Add wrapper `GenerateProjects.bat` / `GenerateProjects.sh`.
- [ ] Generation should be independent from environment bootstrap after the first successful setup.

### T01.11 – Build scripts

- [ ] Add `scripts/build.py`.
- [ ] It should support:
- config selection: `Debug`, `Release`, `Dist`,
- compiler/toolset selection,
- clean build,
- rebuild,
- selected target/project.
- [ ] Add wrapper `Build.bat` / `Build.sh`.
- [ ] Output should clearly identify:
- generator,
- compiler,
- configuration,
- output path.

### T01.12 – Run scripts

- [ ] Add `scripts/run.py`.
- [ ] It should run the already-built app without implicitly rebuilding unless requested.
- [ ] Support runtime flags for:
- debug/dev mode,
- log level,
- project path,
- safe mode,
- reset layout.
- [ ] Add wrapper `Run.bat` / `Run.sh`.

### T01.13 – Build-and-run scripts

- [ ] Add `scripts/build_and_run.py`.
- [ ] It should:
- optionally regenerate,
- build selected config,
- launch the executable.
- [ ] Support explicit compiler selection from one entrypoint.
- [ ] Add wrapper `BuildAndRun.bat` / `BuildAndRun.sh`.
- [ ] This should become the fastest loop for daily development.

### T01.14 – Local CI / doctor tooling

- [ ] Add `scripts/ci_check.py` for local verification.
- [ ] Add `scripts/doctor.py` for machine diagnostics.
- [ ] `ci_check.py` should verify at minimum:
- generation succeeds,
- Debug builds,
- Release builds,
- smoke startup works,
- Python capability check passes.
- [ ] `doctor.py` should print a concise health report of the local environment.

### T01.15 – Documentation bootstrap

- [ ] Initialize `mdBook` in `docs/`.
- [ ] Add minimal `SUMMARY.md`.
- [ ] Add documentation pages for:
- getting started,
- project layout,
- tooling overview,
- setup flow,
- troubleshooting.
- [ ] Add a script/flag to build the docs locally.
- [ ] Add a script/flag to serve docs on localhost.

### T01.16 – App branding and shell polish baseline

- [ ] Add application icon set:
- `.ico` for Windows,
- source `.svg` or high-resolution master asset,
- PNG exports for docs and future packaging.
- [ ] Add a **dark title-bar / window chrome** policy.
- [ ] On Windows, prefer native dark title-bar integration if available.
- [ ] Add a startup banner/splash asset that is visually consistent with the final app identity.
- [ ] Keep this phase functional, not overdesigned: one good branded baseline is enough.

### T01.17 – Acceptance criteria for T01

- [ ] On a clean Windows machine, `Setup.bat` can:
- detect missing tools,
- offer installation or custom path selection,
- prepare `.venv/`,
- install selected Python groups,
- generate Visual Studio files,
- build Debug,
- optionally open VS or VS Code,
- optionally run the app.
- [ ] `GenerateProjects.bat` can regenerate without redoing full setup.
- [ ] `Build.bat` can compile a selected config/compiler.
- [ ] `Run.bat` can launch a previously built executable.
- [ ] `BuildAndRun.bat` can compile and immediately launch.
- [ ] `ExportPythonEnv.bat` can refresh lock/export files.
- [ ] Normal application launch does not spawn an unwanted startup console.

### T01.18 – Suggested dependency split for T01

#### Download / integrate now

These are needed immediately for platform bootstrap, app shell, or the next near-term milestones.

- [ ] Premake5
- [ ] GLFW
- [ ] Dear ImGui (docking)
- [ ] ImPlot
- [ ] ImGuizmo
- [ ] GLM
- [ ] EnTT
- [ ] spdlog
- [ ] Tracy
- [ ] yaml-cpp
- [ ] nlohmann/json
- [ ] pybind11
- [ ] Python
- [ ] uv
- [ ] mdBook

#### Can wait until the related feature starts

These should be registered in the dependency policy, but do not need to be fully integrated in T01.

- [ ] SQLiteCpp
- [ ] zstd
- [ ] minizip-ng
- [ ] cpr
- [ ] libssh2

#### Python packages to install now in the smallest useful Scientific Runtime

- [ ] numpy
- [ ] ASE
- [ ] pymatgen
- [ ] spglib
- [ ] seekpath

#### Python packages that can wait for their dedicated feature slices

- [ ] phonopy
- [ ] sumo
- [ ] doped
- [ ] pydefect
- [ ] PyPhotonics
- [ ] NONRAD / CarrierCapture.py

### T01.19 – Notes and boundaries

- [ ] Do not overbuild the toolchain before the first app shell exists.
- [ ] Do not pull in remote-workflow dependencies too early.
- [ ] Do not make Python a user scripting language yet; keep it as Scientific Runtime infrastructure.
- [ ] Keep wrappers minimal and keep logic centralized in Python.

---

## Revised T03 – Cross-Platform (`task/03-crossplatform`)

**Goal:** make the setup and day-to-day developer flow consistent between Windows and Linux without pretending both platforms are identical.

### T03.1 – Generation parity

- [ ] Add Linux generation through `premake5 gmake2`.
- [ ] Support both `gcc` and `clang` builds.
- [ ] Ensure output folder naming is consistent with Windows.
- [ ] Keep platform-specific switches isolated in Premake, not scattered.

### T03.2 – Script parity

- [ ] `Setup.sh` should mirror `Setup.bat` behavior as closely as practical.
- [ ] `GenerateProjects.sh`, `Build.sh`, `Run.sh`, `BuildAndRun.sh`, `ExportPythonEnv.sh` should exist and map to the same canonical Python scripts.
- [ ] Flags and help text should stay aligned between platforms.
- [ ] Platform-specific defaults may differ, but command names and semantics should not.

### T03.3 – Tool discovery parity

- [ ] Detect on Linux:
- Python,
- uv,
- Premake5,
- gcc,
- clang,
- make,
- mdBook,
- optional VS Code.
- [ ] Support distro-managed tools where practical.
- [ ] Support repo-local managed tools where distro packages are unsuitable.
- [ ] Document which tools are expected from the system and which may be auto-downloaded.

### T03.4 – Python parity

- [ ] The same `pyproject.toml` and `uv.lock` should drive both Windows and Linux.
- [ ] The setup flow should install the same dependency groups where supported.
- [ ] Capability checks should explicitly report platform-specific missing packages or unsupported paths.

### T03.5 – GUI runtime parity

- [ ] Verify GLFW/OpenGL startup on Linux.
- [ ] Verify dark theme and app icon behavior under Linux desktop environments.
- [ ] Add Linux launcher/desktop-entry notes later when packaging starts.
- [ ] Accept that terminal-launched GUI apps on Linux may still inherit terminal stdout/stderr; the important part is that there is no separate unwanted console app model as on Windows.

### T03.6 – Validation matrix

- [ ] Validate at least these flows:
- Windows + VS2022 + MSVC + Debug
- Windows + VS2022 + MSVC + Release
- Windows + gmake + g++ + Debug
- Windows + gmake + g++ + Release
- Windows + gmake + clang++ + Debug
- Windows + gmake + clang++ + Release
- Linux + gmake2 + gcc++ + Debug
- Linux + gmake2 + gcc++ + Debug
- Linux + gmake2 + clang++ + Debug
- Linux + gmake2 + clang++ + Debug
- Add script on linux and windows that will automate this procedure, fresh clone and fresh build
- [ ] Record known gaps explicitly instead of silently diverging.

### T03.7 – Acceptance criteria for T03

- [ ] `Setup.sh` can prepare a clean Linux machine for development.
- [ ] `GenerateProjects.sh` regenerates Linux project files independently.
- [ ] `Build.sh` can compile with selected compiler/config.
- [ ] `Run.sh` launches the app.
- [ ] `BuildAndRun.sh` supports daily development on Linux.
- [ ] `ExportPythonEnv.sh` produces the same lock/export artifacts as on Windows.

---

## Recommended script flags

These are not mandatory final names, but the flow should support equivalent functionality.

### `setup.py`

- `--defaults`
- `--yes`
- `--ci`
- `--dry-run`
- `--skip-git-update`
- `--install-tools`
- `--python-group <name>`
- `--generator <vs2022|gmake2>`
- `--compiler <msvc|gcc|clang>`
- `--config <Debug|Release|Dist>`
- `--open <vs|vscode|none>`
- `--run`

### `generate_projects.py`

- `--generator <vs2022|gmake2>`
- `--compiler <msvc|gcc|clang>`
- `--clean`

### `build.py`

- `--config <Debug|Release|Dist>`
- `--compiler <msvc|gcc|clang>`
- `--target <name>`
- `--clean`
- `--rebuild`

### `run.py`

- `--config <Debug|Release|Dist>`
- `--project <path>`
- `--log-level <trace|debug|info|warn|error>`
- `--safe-mode`
- `--reset-layout`

### `build_and_run.py`

- `--regenerate`
- `--config <Debug|Release|Dist>`
- `--compiler <msvc|gcc|clang>`
- `--project <path>`

### `export_python_env.py`

- `--check-lock`
- `--update-lock`
- `--group <name>`
- `--all-groups`
- `--requirements-out <path>`
- `--freeze-installed`

---

## Practical implementation order

Recommended order of execution:

1. T01.1–T01.4
	Commit checkpoint: when repository baseline, Premake workspace, GUI shell, and minimal runtime foundation build and start successfully on Windows.
2. T01.6–T01.8
	Commit checkpoint: when setup orchestration, tool discovery policy, and Python bootstrap work end-to-end on a clean machine path.
3. T01.10–T01.13
	Commit checkpoint: when generate/build/run/build-and-run scripts form a stable daily developer loop without manual glue steps.
4. T01.14–T01.15
	Commit checkpoint: when ci_check and doctor are usable and mdBook bootstrap pages build locally.
5. T01.16–T01.17
	Commit checkpoint: when branding baseline and T01 acceptance checks are complete and reproducible.
6. T03.1–T03.4
	Commit checkpoint: when Linux generation, script/tool discovery parity, and Python lock/group parity are validated.
7. T03.5–T03.7
	Commit checkpoint: when cross-platform GUI/runtime checks and validation matrix runs are documented with known gaps.
8. deferred dependencies and richer Scientific Runtime groups
	Commit checkpoint: when each dependency slice is integrated as an isolated, reviewable feature increment.

Commit sizing rule: prefer 1-3 commits per stage, each as a single logical unit (small enough for review and rollback).
Commit quality gate: create a commit only after the stage-local checks are green.

This order gives the fastest path to a usable daily developer loop.

---

## One-sentence summary

The main idea is: **one canonical Python orchestration layer, thin OS-specific wrappers, repo-local managed tools where sensible, minimal-but-real GUI app shell from day one, and only the dependencies needed for the immediate MVP foundation downloaded now.**
