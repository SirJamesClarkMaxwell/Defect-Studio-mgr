# Start

Defect Studio documentation is organized as a small hub with section pages and deeper subpages.

Use this page as the entry point.

## What to read first

- [Build and Validation](build-and-validation.md) for local build and test workflows.
- [Runtime Core](runtime-core.md) for application shell, event handling, queueing, and core services.
- [Architecture](architecture.md) for higher-level system design.
- [Reference](reference.md) for shared conventions and key classes.

## Quick map

### Build and Validation

- [Build Matrix](build-matrix.md)
- [Test Matrix (Release/Dist)](test-matrix-release-dist.md)
- [Test Strategy](test-strategy.md)

### Runtime Core

- [System Architecture Overview](architecture-system-overview.md)
- [Core Architecture](architecture-core.md)
- [Application Lifecycle System](application-lifecycle.md)
- [Application Shell System](system-application-shell.md)
- [LayerStack System](system-layer-stack.md)
- [Event Systems](system-events.md)
- [Event Queue System](eventqueue.md)
- [Job System](system-jobs.md)
- [Progress Tracker System](system-progress-tracker.md)
- [Config Manager System](system-config-manager.md)

### Architecture

- [Data Model Architecture](architecture-data-model.md)
- [IO Architecture](architecture-io.md)
- [Renderer Architecture](architecture-renderer.md)
- [Python Bridge Architecture](architecture-python-bridge.md)
- [Layers Architecture](architecture-layers.md)
- [UI Architecture](architecture-ui.md)

### Reference

- [Logging and Profiling](logging.md)
- [API Key Classes](api-key-classes.md)

## Build workflow

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

## Tests (Release/Dist)

Use the dedicated wrapper scripts to build and execute tests for all supported compilers in Release and Dist:

Windows:

```powershell
scripts\Windows\TestReleaseDistTests.bat --continue-on-error
```

Linux:

```bash
bash ./scripts/Linux/TestReleaseDistTests.sh --continue-on-error
```

Details are documented on [Test Matrix (Release/Dist)](test-matrix-release-dist.md).
