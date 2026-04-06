# IMPLEMENTATION PLAN – DefectsStudio

> Execution-oriented implementation guidance for DefectsStudio.  
> This document complements `TODO.md`, the SPEC, ADRs, and milestone tracking.  
> The TODO remains the authoritative backlog. This file explains how implementation should proceed in practice.

## Purpose

This document defines the practical implementation strategy for DefectsStudio.

It does not replace the detailed TODO checklist. Instead, it explains:

- how to interpret the TODO during development
- how to implement features without damaging architecture
- when to formalize broader abstractions
- how to keep the project maintainable during long-term solo development

---

## Core Implementation Strategy

DefectsStudio follows a **TODO-first, abstractions-when-needed** implementation model.

This means:

- the active `TODO.md` is the primary execution plan
- implementation follows the current task order unless there is a strong reason to change it
- architectural boundaries are respected from the beginning
- larger abstractions are introduced only when recurring patterns clearly justify them

The goal is to avoid both extremes:

- under-structured feature hacking
- abstraction-heavy framework building before real needs exist

---

## Primary Development Principles

### 1. Implement the next real need, not the next hypothetical framework
The default question is:

- “What is the next real capability required by the current TODO task?”

not:

- “What general framework might we need much later?”

### 2. Keep architecture strict even when implementation is simple
Simple implementation is acceptable. Sloppy ownership is not.

### 3. Prefer vertical usefulness over disconnected technical islands
Each implementation step should make the workbench more usable or more reproducible.

### 4. Documentation and tests are part of implementation
For DefectsStudio, they are not optional polish.

---

## Non-Negotiable Guardrails

- [ ] folder and namespace boundaries are explicit
- [ ] dependencies follow the agreed architectural direction
- [ ] domain logic is not placed in UI, renderer, or raw scene code
- [ ] Python execution is not scattered arbitrarily through the codebase
- [ ] project persistence is not mixed with parser/export logic
- [ ] ECS is not used as the scientific source of truth
- [ ] new architecture changes are documented with ADRs when meaningful
- [ ] parser, serialization, and runtime boundaries are tested when introduced
- [ ] capability checks are explicit where feature availability may vary
- [ ] large `.cpp` files are actively avoided by splitting responsibilities

---

## Runtime and Domain Shape to Preserve

Implementation should preserve the following runtime shape:

- `Project` = metadata, focus, project-level organization
- `ProjectWorkspace` = open runtime container
- registries = owners of top-level runtime entities
- `Defect` = first-class scientific entity
- `DefectConfiguration` = explicit first-class entity
- `CalculationRecord` = explicit early entity, developed as the scientific workflow grows
- `AnalysisRecord` = project-level analysis entity

Identity rules:

- top-level entities use plain `UUID`
- lower-level objects use local/runtime identifiers

Lifetime rules:

- `Ref<>` wraps `std::shared_ptr`
- `WeakRef<>` wraps `std::weak_ptr`
- UI/ECS should prefer UUID-based lookup and snapshots over owning domain references

---

## Scientific Runtime Rules

Scientific Runtime must be implemented with:

- capability-based public ports
- library-based adapters
- C++ DTO outputs
- explicit version/capability reporting
- smoke tests
- adapter contract tests
- golden/reference tests for scientific workflows

Do not let library-specific concepts leak into the rest of the application.

---

## Persistence Rules

Persistence implementation must preserve:

- `format_version` on persistent files
- explicit migration steps
- project-relative paths through `PathResolver`
- missing-file validation on open
- canonical save separate from recovery save
- lightweight workspace/UI autosave separate from project recovery autosave

---

## Threading Rules

Implementation must preserve:

- main thread for UI, ImGui, OpenGL, renderer lifecycle, final state commit
- background jobs for heavy work
- snapshot-first execution for long-running jobs
- no direct mutation of live project state from background jobs
- cooperative cancellation
- no partial project commit on cancel/error except for explicit dev/debug workflows

Volumetrics specifically must follow the request/commit GPU model:

- background prepares immutable compute inputs
- render thread performs GPU execution
- future CUDA backend remains possible without changing domain ownership

---

## Capability and Diagnostics Rules

The implementation should distinguish between:

### Normal operational diagnostics
Part of the normal UI:
- log panel
- job/task monitor

### DebugLayer
Developer-only deep diagnostics:
- enabled in Debug
- optional in internal Release
- not present in Dist
- read-mostly by default

Feature access rules:
- unavailable capabilities should usually appear disabled with a reason
- validation failures before execution use blocking popups
- execution-time failures use notifications and logs

---

## Recommended Implementation Phases

### Phase 1 — Engineering Foundation
**Tasks:** T01, T02, T03

Primary goals:

- build system
- repository structure
- tooling and scripts
- documentation base
- local CI checks
- sample data for tests
- cross-platform confidence
- initial diagnostics panels

### Phase 2 — Core Runtime and Scientific Boundary
**Tasks:** T04, T05, T06

Primary goals:

- application lifecycle
- event flow
- capability registry
- configuration ownership
- background jobs
- Python scientific runtime
- first stable renderer path
- DebugLayer skeleton

### Phase 3 — Structure Workbench and Scientific Records Foundation
**Tasks:** T07, T08, T09, T10, T11, T12, T13

Primary goals:

- structure data model
- structure import/export
- scene editing
- project persistence
- project UX
- native structure authoring
- runtime ProjectWorkspace and registries
- explicit scientific records
- early clipboard and drag/drop support

### Phase 4 — Volumetrics and Export
**Tasks:** T14, T15

Primary goals:

- scalar-field handling
- volumetric parsing
- isosurface workflows
- request/commit GPU path
- export workflows
- explicit derived-data handling

### Phase 5 — Scientific MVP Completion
**Tasks:** T16, T17, T18, T19

Primary goals:

- convergence workflows
- VASP Ecosystem Integration
- defect thermodynamics
- optical and phonon properties

`CalculationRecord` should become a highlight system during this overall growth path and continue evolving after MVP-1.

---

## When to Formalize New Abstractions

The following abstractions are expected to appear only when earned:

### `AnalysisRunService`
Introduce when multiple scientific workflows clearly share:

- execution lifecycle
- progress/error handling
- persistence pattern
- derived-data behavior
- rerun or reuse semantics

### richer `DerivedDataStorage`
Introduce when derived outputs need:

- versioning
- manifesting
- indexing
- lifecycle policies
- rebuild rules beyond simple conventions

### broader module APIs
Introduce when repeated collaboration patterns justify stable contracts.

### transactional save model
Introduce when canonical save becomes complex enough to require controlled multi-step writes.

### calculation packaging / scientific bundle tooling
This is expected **after MVP-1** as T30 – Calculation Packaging / Scientific Bundles, introduced as an additional import/transport path and possible technical library/tooling boundary. It should not distort current MVP workflows.

---

## Guidance for Technical Library Extraction

By default, implementation should stay inside the modular monolith.

Separate internal libraries, tools, or special targets should only be introduced when a component is:

- sharply isolated
- dependency-heavy in a specialized way
- performance-sensitive
- better tested separately
- reusable across multiple workflows
- cleaner outside the main target

Current likely candidates:

- GPU FNV correction engine
- WAVECAR → KS orbital projection / CHGCAR-oriented engine
- calculation packaging / scientific bundle tooling
- future CUDA volumetric backend

---

## Packaging and Productization Guidance

Before and during MVP:
- focus on portable/internal distribution
- keep feature-gate readiness in the architecture
- do not overbuild licensing/product systems too early

After MVP:
- installer work may start if justified
- file associations and product polish can be added
- capability/policy model can expand into real licensing if needed

---

## Expected Evidence Per Phase

For each major phase or release, implementation should leave behind:

- [ ] updated documentation
- [ ] updated TODO progress
- [ ] tests for the riskiest new boundaries
- [ ] sample data or project references
- [ ] notes about temporary compromises, if any
- [ ] architecture decision updates if the design meaningfully changed

---

## Definition of Healthy Progress

Implementation is on the right track when:

- the next TODO items are being completed without architectural collapse
- the code remains understandable after a pause
- each phase increases practical workflow capability
- scientific workflows become more reproducible
- temporary shortcuts are visible and limited
- future refactoring is still possible

Implementation is **not** healthy if progress comes mainly from:

- bypassing domain ownership
- direct parser calls from UI
- storing project truth in scene state
- scattering Python calls everywhere
- hiding feature-availability assumptions in random UI code
- growing undocumented “temporary” patterns that never get revisited

---

## Role of This Document

This document is the practical bridge between:

- the **SPEC**, which defines intended architecture
- the **ADRs**, which define key decisions
- the **TODO**, which defines detailed work items
- the **Milestones**, which define release-level progress
- and the **Gathering Results** document, which defines how success is evaluated

Its purpose is to keep implementation disciplined, practical, and sustainable over the lifetime of the project.
