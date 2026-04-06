# ADR-007 – Debug Layer and Capability Access Model

- **Status:** Accepted
- **Date:** 2026-04-05
- **Decision Makers:** Project author
- **Related Documents:** `SPEC-1-DefectsStudio-MVP-v2.md`

## Context

DefectsStudio needs strong diagnostics for solo development, but it also needs a clean separation between developer-only tooling and normal product behavior. At the same time, the application must support features whose availability may vary by build, runtime environment, and future product policy.

## Decision

DefectsStudio adopts:

1. a **privileged DebugLayer**
2. a central **CapabilityRegistry**

These are related but distinct concepts.

## DebugLayer

`DebugLayer` is a **developer-only diagnostics layer**.

Rules:

- available in **Debug** builds
- optionally available in **internal Release** builds
- excluded from **Dist** builds
- **read-mostly** by default
- may inspect all major application areas
- may expose explicit dev-only actions
- no production module may depend on it

`DebugLayer` is not the same thing as normal operational diagnostics.

### Normal operational diagnostics
These belong to the standard application UI and remain available in normal builds:

- log panel
- job/task monitor

### Typical DebugLayer content
- deep registry inspectors
- ECS/domain/state inspection
- Storage and DerivedData inspectors
- capability and environment reports
- invariant checks
- explicit dev-only actions such as rebuild/reload/validate/dump

## CapabilityRegistry

`CapabilityRegistry` is the central place that answers:

- is this feature available?
- if not, why not?

It combines three sources:

- **build-time**
- **runtime**
- **policy/access**

### Examples
- `debug.layer`
- `runtime.python`
- `runtime.python.pymatgen`
- `compute.opengl`
- `compute.cuda`
- `module.remote`
- `feature.internal.dev_actions`

## UI policy

The UI uses the following rules:

### Capability unavailable from the start
- feature is shown disabled
- reason is visible through tooltip or equivalent explanation

### Validation failure before execution
Examples:
- missing required inputs
- unsupported selection
- feature not implemented yet

Behavior:
- show **blocking popup**

### Execution started but failed later
Examples:
- parser failed during run
- job failed
- Python runtime failed
- remote transfer failed

Behavior:
- show **notification**
- also log the failure
- optionally provide access to details/log output

## Why this decision was made

- developer diagnostics must be powerful without becoming part of production architecture
- normal users still need operational visibility through log/task tooling
- capability differences between builds and environments must be explicit
- the future product may require internal-only and licensed feature gating

## Acceptance criteria

This ADR is applied successfully when:

- DebugLayer exists only in allowed builds
- production modules do not depend on DebugLayer
- log panel and task monitor remain part of normal UI
- unavailable capabilities are not assumed blindly by the UI
- blocking popups are reserved for pre-execution validation failures
- execution-time failures are reported through notifications/logging
