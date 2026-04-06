# ADR-009 – Domain Runtime Model and Scientific Entity Structure

- **Status:** Accepted
- **Date:** 2026-04-05
- **Decision Makers:** Project author
- **Related Documents:** `SPEC-1-DefectsStudio-MVP-v2.md`, `ADR-002-domain-as-source-of-truth-and-ecs-boundary.md`

## Context

DefectsStudio needs a runtime model that is scalable enough for a growing scientific workbench without turning `Project` into a god object or confusing runtime ownership with persistence.

The project also needs a domain model that reflects real scientific workflows around defects, configurations, calculations, and analyses.

## Decision

DefectsStudio adopts:

- `ProjectWorkspace` as the runtime domain container
- project-scoped registries for top-level entities
- plain `UUID` as the identity of top-level entities
- a scientific-entity model centered on `Structure`, `Defect`, `DefectConfiguration`, `CalculationRecord`, and `AnalysisRecord`

## Runtime shape

### `Project`
`Project` remains a high-level domain entity responsible for:

- project identity
- metadata
- focus
- organizational relationships
- project-level settings

It does **not** directly store the main collections of structures, defects, calculations, and analyses.

### `ProjectWorkspace`
`ProjectWorkspace` is the open runtime model. It contains:

- `Project`
- `StructureRegistry`
- `DefectRegistry`
- `DefectConfigurationRegistry`
- `CalculationRegistry`
- `AnalysisRegistry`

Registries are part of the domain runtime model, not Storage.

## Identity model

Top-level entities use plain `UUID` as both persistent and runtime identity:

- `Project`
- `Structure`
- `Defect`
- `DefectConfiguration`
- `CalculationRecord`
- `AnalysisRecord`

Lower-level objects use local/runtime identifiers instead.

## Ownership and references

- `Ref<>` is a wrapper around `std::shared_ptr`
- `WeakRef<>` is a wrapper around `std::weak_ptr`
- top-level registries own long-lived runtime objects
- UI and ECS prefer UUID-based lookup and snapshots over owning references

## Scientific entities

### `Structure`
One concrete geometry.

### `Defect`
A first-class scientific defect concept, not a single structure.

### `DefectConfiguration`
An explicit first-class entity representing one configuration/arrangement of a defect. A defect may own many configurations.

### `CalculationRecord`
A first-class entity representing one calculation with exactly one input structure.

Most calculation records belong semantically to a `DefectConfiguration`, but the model must also support project-level or structure-level calculations such as:

- host/reference runs
- convergence runs
- phase-stability runs

### `AnalysisRecord`
A project-level entity representing one coherent scientific analysis. It may depend on:

- structures
- defects
- configurations
- calculation records
- other analyses

Analysis dependencies must remain explicit and acyclic.

## Analysis granularity

One analysis is allowed to return multiple related outputs, as long as they answer one coherent scientific question.

Examples:
- convergence analysis
- chemical stability analysis
- defect-configuration screening analysis
- defect thermodynamics analysis
- Fermi-level / compensation analysis
- optical-properties analysis
- comparative-defect analysis

A single plot or single number is not a separate analysis record by itself.

## Acceptance criteria

This ADR is applied successfully when:

- `ProjectWorkspace` exists as the runtime domain container
- registries are project-scoped rather than global singletons
- `Project` remains focused on metadata and high-level relationships
- `DefectConfiguration` and `CalculationRecord` are explicit domain entities
- `AnalysisRecord` is modeled as project-level rather than configuration-owned
