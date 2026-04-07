# Core Architecture

The Core layer contains startup, lifetime, diagnostics, and low-level runtime orchestration.

## Current State

Implemented:

- Application bootstrap and render loop shell.
- Logger bootstrap with console and optional file output.
- Build profiles Debug, Release, and Dist.

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
