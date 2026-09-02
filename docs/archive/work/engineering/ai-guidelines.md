# ai-guidelines.md

## 1. Scope

This document defines how AI should be used when generating, editing, explaining, and reviewing code for DefectsStudio.

It is not a replacement for:
- the project architecture,
- the SPEC,
- ADRs,
- TODO,
- implementation planning,
- or human judgment.

AI is the primary implementation engine for this project.  
The human remains responsible for:
- architectural direction,
- quality control,
- acceptance of generated code,
- and gradual growth in understanding of how the system works.

The purpose of these guidelines is not only to keep the codebase healthy, but also to make the AI-assisted development process educational and inspectable for the project author.

## 2. Core development model

DefectsStudio is developed with the following working model:

- AI may generate most or even all implementation code.
- Human review is mandatory before accepting code into the project.
- The human reviewer is not expected to be able to implement the entire system from scratch without AI.
- AI output must therefore be:
  - readable,
  - inspectable,
  - reviewable in small steps,
  - explainable after the fact,
  - and educational rather than opaque.

The long-term goal is not only to ship the application, but also to ensure that the project author becomes significantly more capable of understanding how such an application is built and evolved.

## 3. Non-negotiable principles

- **Architecture beats convenience.**
- **Small, reviewable changes beat large generated dumps.**
- **Readable code beats clever code.**
- **Explicit ownership beats hidden magic.**
- **Stable boundaries beat short-term speed.**
- **Educational clarity beats impressive complexity.**
- **AI assists implementation, but must not silently redesign the project.**

## 4. Architectural rules AI must respect

AI-generated code must respect the already accepted architecture of the project.

### Mandatory rules

- `Domain` is the runtime source of truth.
- `Scene` / `ECS` is derived and must not become scientific truth.
- Python is allowed only through `ScientificRuntime`.
- `Storage` and `IO` are separate responsibilities.
- `ProjectWorkspace` is the runtime container for the open project.
- Background jobs must not directly mutate live project state.
- Final commit of project-visible runtime state happens on the main thread.
- `DebugLayer` is developer-only and production code must not depend on it.

### AI must never

- introduce ECS as an authoritative scientific model,
- call Python libraries directly from `Domain`, `Presentation`, `Storage`, `IO`, or renderer code,
- mix persistence rules into parser/export code,
- bypass `ProjectWorkspace` and registries with ad hoc global ownership,
- mutate live state directly from jobs,
- introduce new architecture “because it feels cleaner” without explicit request.

If architecture pressure appears, AI should:
1. keep the solution local and conservative,
2. explain the pressure clearly,
3. propose an ADR-worthy change only when the need is real.

## 5. AI role in this project

AI is expected to:

- generate implementation code,
- generate local refactors,
- generate tests,
- generate scaffolding,
- generate documentation updates,
- explain generated code,
- explain trade-offs and alternatives,
- make implementation easier to inspect and reason about.

AI is **not** expected to:

- replace architectural decision-making,
- hide complexity behind unexplained abstractions,
- produce giant framework-first code dumps,
- optimize prematurely,
- silently add dependencies or subsystems.

## 6. Required output style for AI-generated code

All generated code should aim to be:

- **small enough to review calmly,**
- **written in the style of `cpp-guidelines.md`,**
- **consistent with the current task,**
- **boring in a good way,**
- **easy to explain line by line,**
- **easy to modify later by a human who is still learning.**

### AI should prefer

- direct implementations inside agreed boundaries,
- local clarity over generalized frameworks,
- named functions over anonymous logic,
- DTOs and explicit data flow,
- explicit error handling,
- value semantics first,
- `Ref<>` / `WeakRef<>` only when justified,
- modern C++20/23 features when they improve clarity and safety,
- free functions over fake helper classes when state is not required.

### AI should avoid

- deep inheritance trees without strong reason,
- template-heavy infrastructure,
- overuse of lambdas,
- opaque metaprogramming,
- broad “manager” objects,
- magic global state,
- output parameters,
- hidden side effects,
- long chains of callbacks,
- accidental shared ownership.

## 7. Change size and task discipline

AI must work in **small, logically reviewable increments**.

### Rules

- One generated change should close one coherent step.
- AI should not generate multiple subsystems at once unless explicitly requested.
- When a task touches multiple architectural areas, AI must explain why.
- Large implementations should be split into phases or checkpoints.
- AI should prefer scaffolding and gradual fill-in over giant end-state dumps.

### Default mindset

The question is not:
- “How can AI generate the whole feature in one answer?”

The question is:
- “What is the next safe, reviewable, architecturally correct step?”

This matches the project’s TODO-first and abstractions-when-needed development model.

## 8. Explanation requirements

Because the project author is learning through review, AI must not only generate code but also help build understanding.

### AI should explain

- what was changed,
- why it belongs in this layer,
- what ownership model is being used,
- what lifetime assumptions exist,
- what error model is used,
- what the next likely refactor pressure will be,
- what the main risks are.

### AI should not

- dump code without explanation,
- pretend certainty when uncertain,
- hide important trade-offs,
- hand-wave over concurrency, ownership, or persistence.

### Educational rule

Whenever AI generates non-trivial code, it should aim to leave the human reviewer more informed than before.

The goal is:
- not only “code was produced,”
- but also “the reviewer now better understands how such code should be structured.”

## 9. Code generation rules

AI-generated code must follow `cpp-guidelines.md`.

### Mandatory expectations

- Use project naming conventions.
- Respect ownership and lifetime rules.
- Prefer value semantics first.
- Use `Ref<>` / `WeakRef<>` consistently instead of raw `std::shared_ptr` / `std::weak_ptr`.
- Avoid output parameters by default.
- Use the project error model instead of hiding failure behind `bool`.
- Keep classes small and responsibilities focused.
- Prefer free functions when no state is needed.
- Keep lambdas short and local.
- Use modern C++20/23 where it improves clarity.
- Avoid clever code that would be difficult to re-derive manually later.

## 10. Dependency and library discipline

AI must not introduce new libraries casually.

### Rules

- No new library may be added without explicit justification.
- The reason for adding a library must be stated clearly:
  - what problem it solves,
  - why existing project tools are insufficient,
  - what architectural impact it has,
  - and what maintenance burden it introduces.
- If a library affects build system, runtime, packaging, or architecture, AI must call that out explicitly.
- Existing chosen project technologies should be preferred unless there is a strong reason to deviate.

## 11. Refactoring rules

AI may propose refactors, but they must be conservative and justified.

### Good refactors

- reduce file size,
- sharpen ownership,
- improve naming,
- remove duplication,
- improve testability,
- isolate external boundaries,
- improve layer clarity,
- replace accidental complexity with simpler code.

### Bad refactors

- introduce speculative abstractions,
- rewrite working code without strong reason,
- replace understandable code with more “generic” code,
- merge unrelated responsibilities,
- add framework-style layers without proven pressure.

## 12. Testing expectations

AI should treat tests as part of implementation, not as optional polish.

### AI should propose tests for

- parsers,
- serialization,
- ScientificRuntime boundaries,
- result/error translation,
- migration-related persistence behavior,
- snapshot-based workflows,
- capability checks,
- adapter contracts,
- golden/reference scientific workflows where relevant.

### AI should distinguish

- smoke tests,
- contract tests,
- regression tests,
- golden/reference tests.

AI does not need to generate every possible test immediately, but it must not behave as if risky boundaries can remain untested indefinitely.

## 13. Documentation expectations

AI must help keep the project understandable.

### Rules

- If code changes the meaning of a boundary, AI should mention the documentation impact.
- Comments should explain **why**, not narrate obvious syntax.
- AI should update or propose updates to relevant docs when the change justifies it.
- Temporary compromises should be documented briefly and honestly.
- AI should not produce fake documentation for code that does not exist yet.

## 14. Review checklist for AI-generated code

Before accepting AI-generated code, the reviewer should be able to answer:

- Is the layer correct?
- Is ownership clear?
- Is lifetime clear?
- Does the code follow `cpp-guidelines.md`?
- Did AI introduce shared ownership without a strong reason?
- Did AI hide failure behind `bool`?
- Did AI leak Python, UI, renderer, or persistence concerns into the wrong place?
- Is the change small enough to understand?
- Could this still be understood after a month-long break?
- Did the change teach the reviewer something useful?

If the answer to several of these is “no,” the code should be revised before acceptance.

## 15. Preferred response format for AI coding help

When AI generates or modifies code, the preferred response structure is:

1. **What changed**
2. **Why this belongs here**
3. **Main ownership / lifetime decisions**
4. **Boundary risks**
5. **What to review carefully**
6. **Suggested next step**

This format is preferred because it supports both quality control and learning.

## 16. Red flags

The following are red flags in AI-generated output:

- giant unreviewable files,
- unexplained architecture changes,
- surprise dependencies,
- overuse of shared ownership,
- weak naming,
- bool-based hidden failures,
- direct live-state mutation from jobs,
- Python calls outside `ScientificRuntime`,
- scene/ECS becoming persistent scientific truth,
- fake “temporary” hacks without explanation,
- code that is hard to explain back to a human reviewer.

If AI produces such code, the output should be treated as suspect even if it compiles.

## 17. Final principle

The purpose of AI in DefectsStudio is not only to produce code faster.

Its purpose is to help build the application in a way that is:
- architecturally correct,
- reviewable,
- maintainable,
- and educational for the project author.

AI should therefore generate code that the human reviewer can gradually learn from, reason about, and confidently supervise — even if the reviewer could not have written the entire system from scratch alone.
