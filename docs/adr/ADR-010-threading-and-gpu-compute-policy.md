# ADR-010 – Threading and GPU Compute Policy

- **Status:** Accepted
- **Date:** 2026-04-05
- **Decision Makers:** Project author
- **Related Documents:** `SPEC-1-DefectsStudio-MVP-v2.md`

## Context

DefectsStudio combines UI, rendering, file parsing, scientific computation, and volumetric GPU workflows. Without a clear threading contract, the project would become difficult to debug and prone to race conditions and lifetime issues.

## Decision

DefectsStudio adopts the following threading policy:

- UI, ImGui, OpenGL, renderer lifecycle, and final project-visible state commit are main-thread only
- heavy work runs in background jobs
- long-running scientific jobs use snapshot-first execution
- jobs do not mutate live project state directly
- cooperative cancellation is required
- partial project commits on cancel/error are not allowed except for explicit dev/debug workflows

## Main-thread responsibilities

Main thread owns:

- UI
- ImGui
- OpenGL
- GPU resource lifecycle
- scene/render commit
- final commit of project-visible runtime state

## Background jobs

Background jobs are used for:

- parsers
- import preprocessing
- scientific analysis
- volumetric preprocessing
- data normalization
- CPU-side preparation for later GPU work

## Snapshot-first policy

Long-running jobs should receive immutable snapshots of inputs rather than live mutable domain objects.

This applies especially to:

- scientific analyses
- volumetric workflows
- larger parsing/import tasks

## Commit policy

Jobs produce results, DTOs, derived-data outputs, or compute requests. The main thread performs the final commit into the live project state.

## Cancellation policy

Cancellation is cooperative. Jobs receive a cancellation token and are expected to check it at safe points. Cancelled jobs do not partially mutate project state.

## Volumetric GPU policy

Volumetric workflows use a **request/commit model**.

### Model
1. Background jobs prepare immutable volumetric compute inputs
2. A backend-agnostic compute request is produced
3. The render thread consumes the request
4. GPU-side work is executed on the render thread
5. Results are committed as render resources or derived outputs

### Backends
- default MVP backend: OpenGL Compute Shader
- future optional backend: CUDA

The domain and analysis model must remain backend-agnostic.

## Assertions and guards

Thread-affinity checks should be explicit in critical areas, for example through main-thread assertions in UI, renderer, and final commit paths.

## Acceptance criteria

This ADR is applied successfully when:

- renderer and UI remain main-thread only
- long-running jobs use snapshot-first execution
- live project state is not mutated directly from background jobs
- cancellation is cooperative and explicit
- volumetric GPU execution follows the request/commit model
- a future CUDA backend can be introduced without changing domain ownership semantics
