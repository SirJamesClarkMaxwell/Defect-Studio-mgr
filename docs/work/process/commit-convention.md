# Commit Convention

Commit messages must be readable, specific, and useful after a long break.

## Format

<type>(<scope>): <summary>

Example:
- feat(core): add application bootstrap
- feat(renderer): add orbit camera
- fix(project): validate missing file references on open
- refactor(domain): split calculation registry queries
- docs(cpp): add ownership and lifetime rules
- test(io): add POSCAR roundtrip coverage

## Allowed types

- `feat` — new functionality
- `fix` — bug fix
- `refactor` — internal code change without intended behavior change
- `docs` — documentation only
- `test` — tests only
- `build` — build system or tooling changes
- `chore` — maintenance work with no meaningful product behavior change

## Scope

Scope should describe the affected area, for example:
- `core`
- `domain`
- `project`
- `storage`
- `io`
- `renderer`
- `scene`
- `python`
- `thermo`
- `phonons`
- `docs`
- `ci`

## Rules

- Summary must describe what changed, not just that something changed.
- Use imperative style.
- Keep the summary short but specific.
- Do not use vague commit messages such as:
  - `fix`
  - `changes`
  - `update`
  - `misc`
  - `cleanup`
- One commit should ideally represent one logical unit of change.

## Good examples

- feat(core): add layer stack and application loop
- fix(io): preserve POSCAR selective dynamics flags
- refactor(scene): move gizmo selection sync into bridge
- docs(ai): clarify AI task size rules

## Bad examples

- fix
- update code
- stuff
- changes
- final fix maybe
