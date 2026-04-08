# TODO Detailed – T01 / T03 / T04 Expansion

This document expands and reorganizes **T01**, **T03**, and now **T04** so they are immediately actionable and consistent with the current DefectsStudio direction: a **single desktop executable**, a **modular domain monolith**, and Python used as a **Scientific Runtime** rather than as end-user scripting in MVP.

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
- `RunWithTracy.bat` – run app with Tracy profiler auto-start (Windows)
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
- `scripts/run_with_tracy.py`
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
- `RunWithTracy.bat`
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

### Status update (2026-04-07)

- [x] Build matrix wrappers added for Windows and Linux.
- [x] Matrix runner added with `--clean-between-runs` to reduce cross-compiler/config artifact collisions.
- [x] mdBook bootstrap extended with a dedicated build-matrix page.
- [x] Full Windows local CI matrix passed (`msvc`, `gcc`, `clang` x `Debug`, `Release`, `Dist`).
- [x] Added `RunWithTracy.bat` + `run_with_tracy.py` for automatic Tracy startup on Windows.
- [ ] Next focus: GUI subsystem decision and implementation for Windows app target (T01.3).

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
- [ ] It should support:
- Visual Studio 2022,
- future `gmake2`,
- selected architecture,
- selected compiler/toolset,
- cleaning previous generated files if requested.
- [x] Add wrapper `GenerateProjects.bat` / `GenerateProjects.sh`.
- [x] Generation should be independent from environment bootstrap after the first successful setup.

### T01.11 – Build scripts

- [x] Add `scripts/build.py`.
- [ ] It should support:
- config selection: `Debug`, `Release`, `Dist`,
- compiler/toolset selection,
- clean build,
- rebuild,
- selected target/project.
- [x] Add wrapper `Build.bat` / `Build.sh`.
- [ ] Output should clearly identify:
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
- [ ] It should:
- optionally regenerate,
- build selected config,
- launch the executable.
- [x] Support explicit compiler selection from one entrypoint.
- [x] Add wrapper `BuildAndRun.bat` / `BuildAndRun.sh`.
- [ ] This should become the fastest loop for daily development.

### T01.14 – Local CI / doctor tooling

- [x] Add `scripts/ci_check.py` for local verification.
- [x] Add `scripts/doctor.py` for machine diagnostics.
- [ ] `ci_check.py` should verify at minimum:
- generation succeeds,
- Debug builds,
- Release builds,
- smoke startup works,
- Python capability check passes.
- [ ] `doctor.py` should print a concise health report of the local environment.

### T01.15 – Documentation bootstrap

- [x] Initialize `mdBook` in `docs/`.
- [x] Add minimal `SUMMARY.md`.
- [ ] Add documentation pages for:
- getting started,
- project layout,
- tooling overview,
- setup flow,
- troubleshooting.
- [x] Add a script/flag to build the docs locally.
- [x] Add a script/flag to serve docs on localhost.

### T01.16 – App branding and shell polish baseline

- [ ] Add application icon set:
- `.ico` for Windows,
- source `.svg` or high-resolution master asset,
- PNG exports for docs and future packaging.
- [ ] Add a **dark title-bar / window chrome** policy.
- [x] On Windows, prefer native dark title-bar integration if available.
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

- [x] Add Linux generation through `premake5 gmake2`.
- [x] Support both `gcc` and `clang` builds.
- [x] Ensure output folder naming is consistent with Windows.
- [x] Keep platform-specific switches isolated in Premake, not scattered.
- [x] Linux backend direction documented as X11-only for the current GLFW fork.

### T03.2 – Script parity

- [ ] `Setup.sh` should mirror `Setup.bat` behavior as closely as practical.
- [x] `GenerateProjects.sh`, `Build.sh`, `Run.sh`, `BuildAndRun.sh`, `ExportPythonEnv.sh` should exist and map to the same canonical Python scripts.
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

- [x] The same `pyproject.toml` and `uv.lock` should drive both Windows and Linux.
- [ ] The setup flow should install the same dependency groups where supported.
- [ ] Capability checks should explicitly report platform-specific missing packages or unsupported paths.

### T03.5 – GUI runtime parity

- [ ] Verify GLFW/OpenGL startup on Linux.
- [ ] Verify dark theme and app icon behavior under Linux desktop environments.
- [ ] Add Linux launcher/desktop-entry notes later when packaging starts.
- [ ] Accept that terminal-launched GUI apps on Linux may still inherit terminal stdout/stderr; the important part is that there is no separate unwanted console app model as on Windows.

### T03.6 – Validation matrix

- [ ] Validate at least these flows:
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
- [ ] Record known gaps explicitly instead of silently diverging.

### T03.7 – Acceptance criteria for T03

- [ ] `Setup.sh` can prepare a clean Linux machine for development.
- [ ] `GenerateProjects.sh` regenerates Linux project files independently.
- [ ] `Build.sh` can compile with selected compiler/config.
- [ ] `Run.sh` launches the app.
- [ ] `BuildAndRun.sh` supports daily development on Linux.
- [ ] `ExportPythonEnv.sh` produces the same lock/export artifacts as on Windows.

---

## Revised T04 – Core: Application, LayerStack, EventSystem (`task/04-core`)

**Goal:** deliver a stable editor core loop with deterministic layer/event flow, explicit runtime ownership boundaries, and baseline diagnostics suitable for day-to-day development.

### T04 status seed (2026-04-08)

- [x] Task selected as the next implementation focus based on `TODO.md` priorities (P2).
- [x] `Application` lifecycle shell refactored to explicit `Create`/`Run`/`Shutdown` flow with smaller per-frame helpers.
- [x] Window ownership extracted to `App::Window` and platform-specific hooks moved under `src/Core/Platform/`.
- [x] Baseline assertion layer added (`Core/Assert.hpp`, `DS_ASSERT`) for runtime guardrails.
- [x] Logger bootstrap aligned with project-level `LogLevel` abstraction and startup/shutdown phase logging in `Application`.
- [x] Linux platform window hook scaffold added (stub), matching Windows split and keeping cross-platform structure ready.
- [ ] Branch scope should remain focused on runtime core only; no renderer-feature creep.

### T04.1 – Core contracts and folder boundaries

- [ ] Define and document minimal contracts first:
- `Application`
- `Layer`
- `LayerStack`
- `Event` and `EventDispatcher`
- `EventBus`
- `EditorContext` (reference-only)
- `ProjectWorkspace` (runtime source of truth)
- [ ] Verify source placement is aligned with modular monolith boundaries:
- `src/Core/`
- `src/App/`
- `src/Debug/`
- [ ] Add one architecture note in docs that states ownership rules explicitly.

### T04.2 – Smart pointer wrappers and helpers

- [x] Implement `Ref<T>`, `WeakRef<T>`, `CreateRef(...)` wrappers in one small core header.
- [x] Add `Unique<T>` and `CreateUnique(...)` for consistent ownership API.
- [x] Keep wrappers lightweight (alias/helper layer only, no custom allocator policy yet).
- [x] Replace direct `std::shared_ptr` usage in newly touched core files with wrappers.
- [x] Add compile-time smoke checks for wrapper usage in Core/App targets.

### T04.3 – Application lifecycle skeleton

- [ ] Implement Application singleton lifecycle:
- initialize
- run main loop
- shutdown
- [ ] Add `OnUpdate`, `OnEvent`, `OnRender` phases with deterministic call order.
- [ ] Keep loop timing simple first (frame delta + fixed hook points, no complex scheduler).
- [ ] Add explicit startup/shutdown logging (phase-by-phase).

### T04.4 – Layer and LayerStack baseline

- [x] Implement `Layer` base interface:
- `OnAttach`
- `OnDetach`
- `OnUpdate`
- `OnEvent`
- `OnImGuiRender`
- [x] Implement `LayerStack` with:
- push layer
- pop layer
- push overlay
- pop overlay
- [x] Guarantee reverse-order event propagation through stack.
- [x] Add basic lifetime tests (attach/detach ordering).

### T04.5 – Event system and dispatch flow

- [x] Implement event type/category model for minimum set:
- window
- keyboard
- mouse
- touchpad
- [x] Implement dispatcher that supports handled/not-handled propagation.
- [ ] Ensure Application routes OS events into LayerStack in one canonical path.
- [ ] Add debug logging flag for event tracing (off by default, enabled in Debug/internal).

### T04.6 – EventBus for decoupled panel communication

- [x] Add publish-subscribe `EventBus` abstraction (lightweight local implementation for now).
- [x] Support:
- subscribe
- unsubscribe
- publish
- [ ] Keep EventBus for cross-panel/module communication, not for low-level OS input dispatch.
- [ ] Add one small integration example proving panel decoupling.

### T04.7 – Runtime ownership: ProjectWorkspace and EditorContext

- [ ] Implement `ProjectWorkspace` as mutable runtime owner of project/domain state.
- [ ] Implement `EditorContext` as a lightweight reference facade (no duplicated ownership).
- [ ] Add guardrails to prevent accidental state ownership drift into UI layers.
- [ ] Document allowed mutation path (main-thread commit rule).

### T04.8 – ConfigManager baseline

- [ ] Implement `ConfigManager` as the single owner of configuration file IO.
- [ ] Start with:
- `default.yaml`
- `ui_settings.yaml`
- [ ] Add schema/version marker for forward migrations.
- [ ] Ensure load failure path returns structured error and safe fallback defaults.

### T04.9 – JobSystem and progress reporting foundation

- [ ] Add minimal thread-pool JobSystem with:
- queued jobs
- priority tag (basic)
- cooperative cancellation token
- [ ] Implement `ProgressTracker` registration/report API for long tasks.
- [ ] Enforce no direct UI mutation from worker threads.
- [ ] Add one demo job visible in diagnostics UI.

### T04.10 – CapabilityRegistry and capability gates

- [ ] Implement `CapabilityRegistry` / `CapabilityService` baseline for:
- build-time capabilities
- runtime detected capabilities
- policy/access flags
- [ ] Expose read API for UI and diagnostics.
- [ ] Connect capability snapshot to startup diagnostics banner/log output.

### T04.11 – Debug and error/notification policies

- [ ] Add `DebugLayer` skeleton for developer diagnostics (Debug + optional internal Release).
- [ ] Implement structured error model:
- category
- code
- technical context
- user-facing message
- [ ] Implement notification channels:
- `NotificationCenter` (live)
- `NotificationHistory` (persistent session log)
- [ ] Apply policy split:
- blocking popup for pre-execution validation failures
- non-blocking notifications for runtime/execution failures

### T04.12 – Operational diagnostics in normal UI

- [ ] Add baseline log panel.
- [ ] Add job/task monitor panel.
- [ ] Add thread-affinity assertions on critical paths (e.g. `ASSERT_MAIN_THREAD`).
- [ ] Ensure diagnostics are informative but low-noise in normal workflow.

### T04.13 – Test strategy for T04

- [ ] Unit tests:
- [x] LayerStack ordering
- [x] event dispatch propagation rules
- [x] EventBus subscribe/unsubscribe lifecycle
- structured error payload mapping
- [ ] Integration smoke tests:
- app boot
- one frame update/render cycle
- clean shutdown
- [ ] Threading safety checks:
- cancellation token respected
- forbidden main-thread-only mutations from worker threads blocked/asserted

### T04.14 – Suggested implementation slices (commit-friendly)

1. Core pointers + base contracts (`Ref`, `Layer`, event primitives).
2. Application loop + LayerStack integration + tests.
3. Event dispatcher + EventBus + tests.
4. ProjectWorkspace/EditorContext ownership path.
5. ConfigManager + structured error model.
6. JobSystem + ProgressTracker + thread guards + tests.
7. DebugLayer + notifications + diagnostics panels.

Each slice should end with green local checks and minimal doc update.

### T04.15 – Acceptance criteria for T04

- [ ] Application boots into main loop and exits cleanly.
- [ ] Layer push/pop/overlay behavior is deterministic and tested.
- [ ] Input/window events propagate through dispatcher and layers in defined order.
- [ ] EventBus supports decoupled communication without tight panel dependencies.
- [ ] `ProjectWorkspace` is the only runtime owner of project/domain mutable state.
- [ ] `EditorContext` does not become a secondary ownership container.
- [ ] Config load/save is centralized in `ConfigManager`.
- [ ] Structured error + notification policy is implemented and observable in UI.
- [ ] JobSystem cancellation/progress flow works and respects thread-affinity constraints.
- [ ] Debug diagnostics layer is available in Debug/internal Release.

### T04.16 – Boundaries and non-goals

- [ ] Do not implement advanced renderer features in this task.
- [ ] Do not expand into full T10/T11 persistence model yet; keep only ownership foundations needed by core.
- [ ] Do not introduce heavy scripting workflows; Python bridge remains in T05 scope.
- [ ] Prefer minimal viable abstractions that are easy to test and refactor.

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
