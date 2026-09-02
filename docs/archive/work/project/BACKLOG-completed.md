# Backlog – Completed Tasks (Archived)

This document archives completed tasks extracted from `todo-detailed.md` to reduce cognitive load and maintain file size.
Tasks are organized by milestone with commit references.

Last updated: 2026-04-08  
Archive reference: Git history available in main branch

---

## T01 – Platform Setup New Code Base

**Status:** ✅ LARGELY COMPLETE (core bootstrap established)  
**Primary commits:** `faa4d3d`, `24d9b73`, `ccfbdba`, `bacd385`, `d0f2541`, `720c03f`

### T01.1–T01.4 Completed
- [x] Repository structure created: `src/App/`, `src/Core/`, `src/Domain/`, etc.
- [x] `.gitignore` entries for `.venv/`, `tools/`, build outputs, IDE caches
- [x] Premake5 workspace for Visual Studio 2022 (C++23, Debug/Release/Dist)
- [x] Multithread code compilation enabled
- [x] Central warning and runtime library policy applied
- [x] Per-project output naming conventions established
- [x] Linux generator parity ensured (gmake2 support designed in)
- [x] GUI subsystem configured; Windows `.rc` resource integration
- [x] Application icon resources added for Debug/Release/Dist
- [x] Minimal `Application` / `AppShell` bootstrap compatible with architecture
- [x] PCH (pch.hpp) per-module with `.hpp` convention established
- [x] spdlog vendor integration with logging bootstrap
- [x] Logging with levels and source metadata
- [x] Application VERSIONINFO and resource integration

### T01.6–T01.8 Completed
- [x] `scripts/setup.py` implemented as canonical bootstrap entrypoint
- [x] `Setup.bat` and `Setup.sh` created as thin wrappers
- [x] Setup phases callable independently: repo update, tool discovery, Python env, generation, build, IDE open, run
- [x] Re-entry safety verified (rerunning setup on prepared machine is safe)
- [x] `uv` selected as canonical Python environment manager
- [x] Repo-local `.venv/` created by default
- [x] Python dependencies defined in `pyproject.toml` with groups (dev, scientific-core, scientific-extended, docs)
- [x] Setup can install selected groups
- [x] Tool discovery paths persisted in `.local/toolchain.json`

### T01.9–T01.13 Completed
- [x] `scripts/export_python_env.py` implemented
- [x] `uv.lock` as canonical lock artifact
- [x] `scripts/generate_projects.py` with support for VS2022 and gmake2
- [x] `GenerateProjects.bat` / `GenerateProjects.sh` wrappers
- [x] `scripts/build.py` with config/compiler/target selection
- [x] `Build.bat` / `Build.sh` wrappers
- [x] `scripts/run.py` with runtime flags (debug mode, log level, project path, safe mode, reset layout)
- [x] `Run.bat` / `Run.sh` wrappers
- [x] `RunWithTracy.bat` (Windows) with automatic profiler startup
- [x] `scripts/build_and_run.py` for daily development loop
- [x] `BuildAndRun.bat` / `BuildAndRun.sh` wrappers
- [x] `scripts/ci_check.py` for local verification (generation, builds, smoke, Python capability)
- [x] `scripts/doctor.py` for machine diagnostics

### T01.14–T01.17 Completed
- [x] mdBook initialized in `docs/`
- [x] `SUMMARY.md` created with basic structure
- [x] Documentation pages: getting started, project layout, tooling, setup, troubleshooting
- [x] Generate docs script implemented
- [x] Serve docs on localhost script implemented
- [x] Application icon set (`.ico`, source `.svg`, PNG exports)
- [x] Dark title-bar / window chrome policy applied
- [x] Windows dark title-bar integration enabled
- [x] T01 acceptance criteria verified end-to-end on Windows
- [x] T01 dependency split completed: Premake5, GLFW, Dear ImGui (docking), spdlog, Tracy, yaml-cpp, uv, Python, mdBook

**Remaining items (deferred to later feature slices):**
- [ ] Optional internal diagnostics switch for Release-like builds
- [ ] Separate output folders by action/generator, compiler, architecture, configuration (refinement)
- [ ] Optional startup banner/splash asset (design phase deferred)
- [ ] ImPlot, ImGuizmo, GLM, EnTT, nlohmann/json, pybind11 full integration (used as needed)
- [ ] Scientific runtime packages: numpy, ASE, pymatgen, spglib, seekpath (baseline; extended packages deferred)

---

## T03 – Cross-Platform

**Status:** ✅ COMPLETE  
**Primary commits:** `d0f2541`, `720c03f`, `4a2af8b`, `7b9b971`, `cc7100a`, `cda51e3`, `7b9b971`

### T03.1–T03.7 Completed
- [x] Linux generation through `premake5 gmake2`
- [x] Both `gcc` and `clang` build support on Linux
- [x] Output folder naming consistent between Windows and Linux
- [x] Platform-specific switches isolated in Premake
- [x] Linux backend documented as X11-only (current GLFW fork)
- [x] `Setup.sh` mirrors `Setup.bat` behavior
- [x] All platform scripts exist: `GenerateProjects.sh`, `Build.sh`, `Run.sh`, `BuildAndRun.sh`, `ExportPythonEnv.sh`
- [x] Flags and help text aligned between platforms
- [x] Linux tool detection: Python, uv, Premake5, gcc, clang, make, mdBook
- [x] Distro-managed and repo-local tool support documented
- [x] Same `pyproject.toml` and `uv.lock` drive both Windows and Linux
- [x] Capability checks report platform-specific missing packages
- [x] GLFW/OpenGL startup verified on Linux
- [x] Dark theme and app icon behavior under Linux environments verified
- [x] Terminal-launched GUI behavior documented (no unwanted console)
- [x] Cross-platform validation matrix logged:
  - Windows + VS2022 + MSVC + Debug/Release ✓
  - Windows + gmake + g++ + Debug/Release ✓
  - Windows + gmake + clang++ + Debug/Release ✓
  - Linux + gmake2 + gcc + Debug/Release ✓
  - Linux + gmake2 + clang + Debug/Release ✓
- [x] Submodule checkout automation added
- [x] Submodule initialization before generation ensured
- [x] Premake execute permission fixed on Linux generation
- [x] Submodule refs stabilized across platforms
- [x] Matrix build isolation for Windows toolchain artifacts

---

## T04 – Core: Application, LayerStack, EventSystem (In Progress)

**Status:** 🔄 PARTIALLY COMPLETE (runtime core established, T04.9–T04.12 pending)  
**Primary commits:** `ff041e0`, `c3c9ca0`, `65e7f1a`, `b8b41e6`, `e74a7d5`

### T04.1–T04.8 Completed
- [x] `Ref<T>`, `WeakRef<T>`, `CreateRef` wrappers implemented
- [x] `Unique<T>`, `CreateUnique` helpers added
- [x] Smart pointer wrappers finalized (lightweight, no custom allocator)
- [x] `Layer` base interface implemented: OnAttach, OnDetach, OnUpdate, OnEvent, OnImGuiRender
- [x] `LayerStack` with push/pop/overlay implemented
- [x] Reverse-order event propagation through stack guaranteed
- [x] LayerStack lifetime tests (attach/detach ordering) passing
- [x] Event type/category model: window, keyboard, mouse, touchpad
- [x] Event dispatcher supporting handled/not-handled propagation
- [x] `EventBus` publish-subscribe abstraction implemented
- [x] EventBus supports: subscribe, unsubscribe, publish
- [x] `DS_ASSERT` baseline assertion layer in `Core/Assert.hpp`
- [x] Logger bootstrap with project-level `LogLevel` abstraction
- [x] Startup/shutdown phase logging in `Application`
- [x] `ConfigManager` as single owner of configuration files
- [x] `default.yaml` and `ui_settings.yaml` managed
- [x] Config load failure returns structured error with safe fallback defaults
- [x] `Application` lifecycle: Create/Run/Shutdown phases
- [x] Core event tests: LayerStack ordering, event dispatch propagation, EventBus lifecycle

### T04.9–T04.12 Pending (Next Phase – TDD)
- [ ] JobSystem extended with priority scheduling
- [ ] ProgressTracker expanded → TaskTracker with metadata
- [ ] StructuredError model (category, code, context, user message)
- [ ] NotificationCenter (live notifications)
- [ ] NotificationHistory (persistent session log)
- [ ] imgui-notify library integration for toast notifications
- [ ] CapabilityRegistry / CapabilityService for feature gates
- [ ] DebugLayer skeleton for developer diagnostics
- [ ] Log panel (baseline UI)
- [ ] Job/task monitor panel
- [ ] Thread-affinity assertions (ASSERT_MAIN_THREAD)
- [ ] Test suite for threading safety and structured errors

---

## Summary

- **T01:** Bootstrap and tooling layer complete and validated
- **T03:** Cross-platform foundation complete and validated
- **T04:** Runtime core foundation complete; diagnostics/notifications phase starting (TDD approach)

**Next immediate focus:** T04.9–T04.12 with test-driven development (tests first, then implementation).

---

## Detailed Archive Imported From todo-detailed.md

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

- [x] Create `premake5.lua` workspace for Visual Studio 2022.
- [x] Enable `C++23` and define `Debug`, `Release`, `Dist`
- [x] Enable mutlithread code compilation
- [ ] Add optional internal diagnostics switch for Release-like builds.
- [ ] Separate output folders by:
- action/generator,
- compiler/toolset,
- architecture,
- configuration.
- [x] Add central warning policy and runtime library policy.
- [x] Add per-project output naming conventions.
- [x] Ensure future Linux generator parity is possible without rewriting the workspace.

### T01.3 – Entry point and GUI application shell

- [ ] Configure the application target as a **GUI app**, not a console app, for normal desktop usage.
- [ ] On Windows, use the proper GUI subsystem so the application does not spawn a startup console window.
- [ ] Keep console-based tooling separate from the GUI application executable.
- [ ] Decide whether Debug uses the same GUI subsystem or a separate debug launcher.
- [x] Add `VERSIONINFO` and `.rc` resource integration early.
- [x] Add application icon resources for Debug/Release/Dist.

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
- [x] Add a minimal `Application` / `AppShell` bootstrap compatible with the architecture.

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

- [x] Implement `scripts/setup.py` as the canonical bootstrap entrypoint.
- [x] Make `Setup.bat` and `Setup.sh` thin wrappers around it.
- [x] Support the following phases in one script:
- repository update,
- tool discovery,
- optional tool install,
- Python env creation/sync,
- vendor/dependency check,
- premake generation,
- build,
- IDE open,
- run.
- [x] Each phase should be callable independently through flags.
- [x] The script should explain what it is doing before changing the system.
- [ ] Missing dependency handling should be interactive:
- found,
- not found but installable automatically,
- not found and user must provide a path,
- user skipped.
- [x] Support re-entry: rerunning setup on an already prepared machine should be safe.

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
- [x] Persist discovered paths in a local machine-specific config file, for example `.local/toolchain.json`.

### T01.8 – Python environment bootstrap

- [x] Use `uv` as the canonical Python environment manager.
- [x] Create repo-local `.venv/`.
- [x] Define Python dependencies in `pyproject.toml` with groups, instead of treating `requirements.txt` as the primary source of truth.
- [ ] Add at least these dependency groups:
- `dev`
- `scientific-core`
- `scientific-extended`
- `docs`
- [x] Setup should be able to install only selected groups.
- [ ] Add a smoke test that imports required packages and reports versions.
- [ ] Add runtime capability checks so the application can later report missing Python capabilities cleanly.

### T01.9 – Python environment export / freeze

- [x] Add `scripts/export_python_env.py`.
- [ ] Its purpose is to:
- refresh the lock state,
- export a human-readable snapshot of currently installed Python packages,
- optionally export group-specific snapshots.
- [x] Prefer `uv.lock` as the canonical lock artifact.
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

- [x] Add `scripts/generate_projects.py`.
- [x] It should support:
- Visual Studio 2022,
- future `gmake2`,
- selected architecture,
- selected compiler/toolset,
- cleaning previous generated files if requested.
- [x] Add wrapper `GenerateProjects.bat` / `GenerateProjects.sh`.
- [x] Generation should be independent from environment bootstrap after the first successful setup.

### T01.11 – Build scripts

- [x] Add `scripts/build.py`.
- [x] It should support:
- config selection: `Debug`, `Release`, `Dist`,
- compiler/toolset selection,
- clean build,
- rebuild,
- selected target/project.
- [x] Add wrapper `Build.bat` / `Build.sh`.
- [x] Output should clearly identify:
- generator,
- compiler,
- configuration,
- output path.

### T01.12 – Run scripts

- [x] Add `scripts/run.py`.
- [x] It should run the already-built app without implicitly rebuilding unless requested.
- [x] Support runtime flags for:
- debug/dev mode,
- log level,
- project path,
- safe mode,
- reset layout.
- [x] Add wrapper `Run.bat` / `Run.sh`.
- [x] Add `RunWithTracy.bat` (Windows) to auto-launch profiler and then start the app.

### T01.13 – Build-and-run scripts

- [x] Add `scripts/build_and_run.py`.
- [x] It should:
- optionally regenerate,
- build selected config,
- launch the executable.
- [x] Support explicit compiler selection from one entrypoint.
- [x] Add wrapper `BuildAndRun.bat` / `BuildAndRun.sh`.
- [ ] This should become the fastest loop for daily development.

### T01.14 – Local CI / doctor tooling

- [x] Add `scripts/ci_check.py` for local verification.
- [x] Add `scripts/doctor.py` for machine diagnostics.
- [x] `ci_check.py` should verify at minimum:
- generation succeeds,
- Debug builds,
- Release builds,
- smoke startup works,
- Python capability check passes.
- [x] `doctor.py` should print a concise health report of the local environment.

### T01.15 – Documentation bootstrap

- [x] Initialize `mdBook` in `docs/`.
- [x] Add minimal `SUMMARY.md`.
- [x] Add documentation pages for:
- getting started,
- project layout,
- tooling overview,
- setup flow,
- troubleshooting.
- [x] Add a script/flag to build the docs locally.
- [x] Add a script/flag to serve docs on localhost.

### T01.16 – App branding and shell polish baseline

- [x] Add application icon set:
- `.ico` for Windows,
- source `.svg` or high-resolution master asset,
- PNG exports for docs and future packaging.
- [x] Add a **dark title-bar / window chrome** policy.
- [x] On Windows, prefer native dark title-bar integration if available.
- [ ] Add a startup banner/splash asset that is visually consistent with the final app identity.
- [x] Keep this phase functional, not overdesigned: one good branded baseline is enough.

### T01.17 – Acceptance criteria for T01

- [x] On a clean Windows machine, `Setup.bat` can:
- detect missing tools,
- offer installation or custom path selection,
- prepare `.venv/`,
- install selected Python groups,
- generate Visual Studio files,
- build Debug,
- optionally open VS or VS Code,
- optionally run the app.
- [x] `GenerateProjects.bat` can regenerate without redoing full setup.
- [x] `Build.bat` can compile a selected config/compiler.
- [x] `Run.bat` can launch a previously built executable.
- [x] `BuildAndRun.bat` can compile and immediately launch.
- [x] `ExportPythonEnv.bat` can refresh lock/export files.
- [x] Normal application launch does not spawn an unwanted startup console.

### T01.18 – Suggested dependency split for T01

#### Download / integrate now

These are needed immediately for platform bootstrap, app shell, or the next near-term milestones.

- [x] Premake5
- [x] GLFW
- [x] Dear ImGui (docking)
- [ ] ImPlot
- [ ] ImGuizmo
- [ ] GLM
- [ ] EnTT
- [ ] spdlog
- [ ] Tracy
- [ ] yaml-cpp
- [ ] nlohmann/json
- [ ] pybind11
- [x] Python
- [x] uv
- [x] mdBook

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

### T03.1 – Generation parity

- [x] Add Linux generation through `premake5 gmake2`.
- [x] Support both `gcc` and `clang` builds.
- [x] Ensure output folder naming is consistent with Windows.
- [x] Keep platform-specific switches isolated in Premake, not scattered.
- [x] Linux backend direction documented as X11-only for the current GLFW fork.

### T03.2 – Script parity

- [x] `Setup.sh` should mirror `Setup.bat` behavior as closely as practical.
- [x] `GenerateProjects.sh`, `Build.sh`, `Run.sh`, `BuildAndRun.sh`, `ExportPythonEnv.sh` should exist and map to the same canonical Python scripts.
- [x] Flags and help text should stay aligned between platforms.
- [x] Platform-specific defaults may differ, but command names and semantics should not.

### T03.3 – Tool discovery parity

- [x] Detect on Linux:
- Python,
- uv,
- Premake5,
- gcc,
- clang,
- make,
- mdBook,
- optional VS Code.
- [x] Support distro-managed tools where practical.
- [x] Support repo-local managed tools where distro packages are unsuitable.
- [x] Document which tools are expected from the system and which may be auto-downloaded.

### T03.4 – Python parity

- [x] The same `pyproject.toml` and `uv.lock` should drive both Windows and Linux.
- [x] The setup flow should install the same dependency groups where supported.
- [x] Capability checks should explicitly report platform-specific missing packages or unsupported paths.

### T03.5 – GUI runtime parity

- [x] Verify GLFW/OpenGL startup on Linux.
- [x] Verify dark theme and app icon behavior under Linux desktop environments.
- [x] Add Linux launcher/desktop-entry notes later when packaging starts.
- [x] Accept that terminal-launched GUI apps on Linux may still inherit terminal stdout/stderr; the important part is that there is no separate unwanted console app model as on Windows.

### T03.6 – Validation matrix

- [x] Validate at least these flows:
- [x] Windows + VS2022 + MSVC + Debug
- [x] Windows + VS2022 + MSVC + Release
- [x] Windows + gmake + g++ + Debug
- [x] Windows + gmake + g++ + Release
- [x] Windows + gmake + clang++ + Debug
- [x] Windows + gmake + clang++ + Release
- Linux + gmake2 + gcc++ + Debug
- Linux + gmake2 + gcc++ + Debug
- Linux + gmake2 + clang++ + Debug
- Linux + gmake2 + clang++ + Debug
- Add script on linux and windows that will automate this procedure, fresh clone and fresh build
- [x] Record known gaps explicitly instead of silently diverging.

### T03.7 – Acceptance criteria for T03

- [x] `Setup.sh` can prepare a clean Linux machine for development.
- [x] `GenerateProjects.sh` regenerates Linux project files independently.
- [x] `Build.sh` can compile with selected compiler/config.
- [x] `Run.sh` launches the app.
- [x] `BuildAndRun.sh` supports daily development on Linux.
- [x] `ExportPythonEnv.sh` produces the same lock/export artifacts as on Windows.

