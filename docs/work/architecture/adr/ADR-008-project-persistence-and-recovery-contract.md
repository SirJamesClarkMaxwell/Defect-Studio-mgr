# ADR-008 – Project Persistence and Recovery Contract

- **Status:** Accepted
- **Date:** 2026-04-05
- **Decision Makers:** Project author
- **Related Documents:** `SPEC-1-DefectsStudio-MVP-v2.md`, `ADR-004-storage-vs-io-split.md`

## Context

DefectsStudio is a project-centric scientific application. Project portability, save/load stability, schema evolution, autosave, and recovery must be treated as explicit architectural concerns from the beginning.

## Decision

DefectsStudio adopts the following persistence contract:

- all persistent project files use explicit `format_version`
- schema evolution uses a lightweight migration pipeline
- project-internal paths are stored relative to `ProjectRoot`
- path normalization goes through `PathResolver`
- project open validates missing files
- manual save is the canonical save
- project autosave writes recovery/snapshot state
- workspace/UI autosave writes lighter session/workspace state more frequently

## Project metadata

Every project should expose stable metadata including at least:

- `uuid`
- `name`
- `created_at`
- `last_modified`
- `format_version`

Additional metadata such as description or primary focus may evolve later.

## Relative paths

Project-local references must use paths relative to `ProjectRoot` wherever possible. Absolute paths are allowed only for explicitly external references.

## Migration

Loading old project files should follow explicit versioned migration steps rather than ad hoc “best effort” parsing everywhere.

## Save model

### Canonical save
Triggered by explicit save. This is the authoritative persisted project state.

### Project autosave
Writes a recovery/snapshot form of project state intended for crash recovery and timed safety saves.

### Workspace/UI autosave
Persists lightweight state such as layout, panel visibility, camera/view state, sliders, and similar workspace/session details.

## Validation on open

When a project is opened, the system should detect missing references and support user-facing actions such as:

- relink
- ignore if safe
- mark for rebuild when derived data is missing

## Why this decision was made

- project portability matters
- format evolution is unavoidable
- recovery needs differ from canonical save semantics
- UI/workspace state changes more frequently than domain/project state
- silent persistence assumptions are too risky for a scientific workbench

## Acceptance criteria

This ADR is applied successfully when:

- all persistent project files have explicit format versions
- path handling is normalized through one clear mechanism
- canonical save and recovery save are distinct concepts
- lightweight UI/workspace autosave exists separately from project recovery
- missing-file validation happens at project open time
