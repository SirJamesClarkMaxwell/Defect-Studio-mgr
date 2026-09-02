---
name: architecture-boundary-review
description: "Use before merging any branch to main in this repo, or when reviewing AI-generated or human code changes that touch more than one of src/{Domain,Renderer,IO,Presentation,App}. Grep-based check of the 6 hard architectural boundaries documented in AGENTS.md / docs/work/project/TODO.md ('Granice architektoniczne'): Domain isolation from UI/Renderer/App, Renderer not being the domain source of truth, IO staying free of domain-to-view logic, Presentation not silently mutating other modules, App staying a composition root instead of a domain orchestrator, and cross-layer communication routed through EventBus/CommandRegistry/JobSystem/StructuredError. Report-only, no build required, complements full-build-verify (which checks compilation/tests, not architecture)."
---

# architecture-boundary-review

Grep-based check of the 6 hard boundaries in `AGENTS.md` / `docs/work/project/TODO.md`
("Granice architektoniczne"). Report findings only — never edit files as part of this skill.

Scope to files changed on the current branch when reviewing a branch for merge
(`git diff --name-only main...HEAD -- src/`); scope to the whole tree when auditing from
scratch. Real `src/` layout: `App`, `Core`, `Debug`, `Demo`, `Domain`, `Events`, `IO`,
`Presentation`, `Renderer`, `ScientificRuntime`, `Storage`.

## 1. Domain isolation
Domain must not depend on UI, Renderer, or App.
```
grep -rn "#include.*\"\(Presentation\|Renderer\|App\)/" src/Domain/
```
Any hit is a violation — Domain types stay pure C++ with no upward includes.

## 2. Renderer is not the domain source of truth
Renderer may *read* domain types to build a snapshot, but must not be where domain state is
authoritatively created or mutated outside a command.
```
grep -rln "DomainLayer\|ProjectWorkspace" src/Renderer/ | grep -v "StructureRendererDataBuilder\|RendererStartupComposer\|src/Renderer/Commands/"
```
`src/Renderer/Commands/` is excluded on purpose: those are `ICommand` classes holding a
`WeakRef<DomainLayer>` that only touch it inside `Execute()`/`Undo()` — that's boundary 6's
sanctioned command channel, not a violation. A hit anywhere else needs justification: check
whether it should instead go through a `Domain` registry method reached via a command.

## 3. IO stays free of domain-to-view logic
The `CrystalStructure -> RendererStructureData` bridge lives in `Renderer/StructureRendererDataBuilder`,
not `IO`.
```
grep -rln "RendererStructureData" src/IO/
```
Any match is a violation — that conversion logic belongs in Renderer, not IO.

## 4. Presentation doesn't silently mutate other modules
Presentation renders UI and collects intent; state changes route through `CommandRegistry`/
`EventBus`, not direct setters reaching into other layers.
```
grep -rn "->Set\|->Apply\|\.Set(" src/Presentation/ | grep -v "ImGui::\|Set[A-Za-z]*Popup\|SetNextWindow\|SetTooltip\|SetItemDefault"
```
Skim survivors for direct mutation of `RendererLayer`/`DomainLayer`/`ProjectWorkspace` state
outside a Command/event dispatch — that's the real signal; ImGui's own `Set*` calls are noise
already filtered out above.

## 5. App stays a composition root
`App` composes systems; it should not contain domain business logic (defect/structure transforms).
```
grep -rln "ApplyVacancy\|ApplyInterstitial\|BondGenerator\|StructureComparison" src/App/
```
A match is not automatically a violation — `RendererStartupComposer.cpp` and
`RendererRuntimeOpenCoordinator.cpp` legitimately `#include "Domain/Crystal/BondGenerator.hpp"`
and *call* it to orchestrate startup, which is exactly what a composition root does. Read the
matched lines: calling into `Domain`'s public API is fine, the violation is App *reimplementing*
that logic inline instead of calling it.

## 6. Cross-layer communication uses the sanctioned channels
Long operations go through `JobSystem`/`ProgressTracker`, user actions through `CommandRegistry`,
errors as `StructuredError`. Spot-check new cross-layer calls in the diff:
```
git diff main...HEAD -- src/ | grep -n "^+" | grep -E "std::thread|throw "
```
A raw `std::thread` outside `JobSystem`, or a `throw` outside `src/Renderer/` (the only
documented exception-free zone — a throw there is checked by the `render_path_exception_guard`
hook already, not this skill) is worth a second look: confirm it's routed through
`StructuredError`/an event instead of an ad-hoc cross-layer call.

## Report
One line per boundary: `PASS` or `VIOLATION: file:line — what and why`. If everything passes,
say so plainly — don't re-scan "to be sure".
