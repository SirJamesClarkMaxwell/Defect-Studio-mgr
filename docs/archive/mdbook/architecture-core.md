# Core Architecture

The Core layer contains startup, lifetime, diagnostics, and low-level runtime orchestration.

## Current State

Implemented:

- Application bootstrap and render loop shell.
- Lifecycle guard state for idempotent `Create`/`Shutdown` behavior.
- Logger bootstrap with console and optional file output.
- Build profiles Debug, Release, and Dist.
- The Application composition root already owns early runtime services such as `EventBus`, `JobSystem`, and `ProgressTracker`.

See dedicated page: [Application Lifecycle System](application-lifecycle.md).

## Target Responsibilities

Planned core responsibilities from TODO:

- Application singleton lifecycle and main-thread policy.
- LayerStack and overlay ordering.
- Event system and EventBus contracts.
- Job system with cancellation and progress reporting.
- Debug-layer capability gates for developer diagnostics.

## Boundaries

Core should not own domain persistence or scientific algorithms.
Those belong to Domain, Storage, and ScientificRuntime layers.
