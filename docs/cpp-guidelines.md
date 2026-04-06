# C++ Guidelines

## 1. Scope

This document defines **C++ coding practices** for DefectsStudio.

It is **not** a replacement for the architecture documentation.  
Architecture remains defined by the SPEC, ADRs, TODO, and implementation plan.

This document focuses on:
- code style and naming,
- ownership and lifetime,
- function and API design,
- file and class organization,
- error handling,
- includes and headers,
- concurrency rules,
- testability and reviewability.

A small number of architectural guardrails are repeated here only when they directly affect everyday coding.

## 2. Architectural guardrails

These rules are non-negotiable and apply to all C++ code in the project.

- `Domain` is the runtime source of truth.
- `Scene` / `ECS` is a derived representation and must never become the authoritative scientific model.
- Python access is allowed only through `ScientificRuntime`.
- `Storage` and `IO` are different responsibilities and must not be collapsed into one mixed layer.
- `ProjectWorkspace` is the runtime container for the open project.
- Background jobs must not directly mutate live project state.
- Final commit of project-visible runtime state happens on the main thread.
- `DebugLayer` is developer-only and production code must not depend on it.

These are guardrails, not the main topic of this document.

## 3. Ownership and lifetime

### Goal

Code must communicate ownership, lifetime, and sharing cost clearly.

### Rules

- Prefer **value semantics first**.
- If an object does not need heap allocation or shared ownership, keep it as a normal value.
- Do not introduce shared ownership without a clear reason.
- Use **`Ref<T>`** and **`WeakRef<T>`** consistently across the project instead of raw `std::shared_ptr` / `std::weak_ptr`.
- Use **`Ref<T>`** only for larger, shared, long-lived runtime objects.
- Use **`WeakRef<T>`** for observation without ownership and for breaking reference cycles.
- If heap ownership is required but the owner is unique, prefer **`std::unique_ptr<T>`**.
- Do not use raw `new` / `delete` in normal application code.
- UI and ECS should not hold long-lived owning references to domain objects. Prefer UUID lookup and snapshots.
- Long-running jobs must not hold mutable live project state. They operate on snapshots and return results for later main-thread commit.

### Practical rules

- Do not introduce `Ref<>` “just in case”.
- Do not allow objects to stay alive only because a random shared reference was accidentally left behind.
- Lifetime should be understandable from the type, the field, and the function signature.
- Shared ownership is the exception, not the default model.

## 4. Naming conventions

### Rules

- **Classes, structs, enums, type aliases, namespaces:** `PascalCase`
- **Public methods:** `PascalCase`
- **Private methods:** `pascalCase`
- **Private member fields:** `m_Name`
- **Static member fields:** `s_Name`
- **Global variables:** `g_Name`
- **Local variables and parameters:** `name`
- **Macros:** `ALL_CAPS`
- **`enum class` values:** `PascalCase`

### Constants

- Default style for constants may be `kName`.
- **Scientific and physical constants may intentionally deviate** when symbolic notation is more natural and more readable.
- Accepted examples include:
  - `q`
  - `k_B`
  - `N_A`

Readability in scientific code takes priority over mechanical uniformity when the notation is standard and unambiguous.

## 5. Class layout

### Order

Classes should use the following order:

1. `public` methods
2. `public` fields
3. `protected` methods
4. `protected` fields
5. `private` methods
6. `private` fields

### Rules

- Methods and fields of the same visibility level should be separated by an explicit visibility section.
- Do not mix methods and fields in one section without reason.
- Private fields should be placed at the end of the class.
- `protected` should follow the same structural discipline as `public` and `private`.

## 6. Classes, files, and responsibilities

### Goal

Code should be easy to understand after a break, easy to review, and easy to split without redesigning half the project.

### Rules

- One class should have one main responsibility.
- Avoid god objects.
- If a class starts holding state, orchestrating workflow, validating input, logging, serializing, and also handling UI, it is poorly shaped.
- Large `.cpp` files are generally discouraged. Roughly 500 lines is a warning sign, although not an absolute rule.
- The 500-line warning does **not** automatically apply to files that intentionally group a coherent API.
- Do not create helper classes just to “put code somewhere”.
- If logic does not require state, prefer **free functions** over artificial helper classes.
- If responsibility is small and local, prefer a private method or a free function over a new abstraction.
- Introduce broader abstractions only when a pattern clearly repeats.

### Practical rules

- A file will usually have one main reason to exist, but related types may be grouped if that improves API clarity.
- Headers should declare as little as reasonably possible.
- Helper implementation that does not need to be public should stay out of headers.
- Prefer a few small, predictable pieces over one clever but fragile construct.

## 7. Functions and API design

### Goal

APIs should be understandable from the signature alone.

A function should clearly communicate:
- what it does,
- what it needs,
- what it returns,
- whether the result may be absent,
- whether it may fail.

### Rules

- Functions should do one readable thing.
- Prefer returning values over output parameters.
- **Output parameters are forbidden by default**, except in low-level, interoperability, or clearly justified performance-sensitive cases.
- If a result may be absent and this is not an error, prefer `std::optional<T>`.
- If an operation may fail, prefer a **project-owned typed error model / result type**.
- Do not hide failure behind `bool`, a dummy empty object, or a comment.
- Mutating functions should be named so that the state change is obvious.
- Read-only member functions should be `const`.
- Pass small semantic types by value when appropriate.
- Pass large read-only inputs by `const T&`.
- Ownership must not be hidden in the signature.
- If a function does not need class state, prefer a free function.
- Do not build abstraction-heavy API layers before there is a real repeated need.

### Naming rules for APIs

- `Get...` for retrieving an existing value
- `Set...` for explicit mutation
- `Is...`, `Has...`, `Can...`, `Should...` for predicates
- `Create...` for creation
- `Load...`, `Save...`, `Import...`, `Export...` for IO and persistence
- `Try...` only when failure is an expected control-flow variant and not a rich operational error
- Avoid vague names such as `Process`, `Handle`, or `Execute` when they do not communicate a concrete purpose

## 8. Lambdas and anonymous functions

### Goal

Do not hide meaningful logic inside unnamed local code blocks.

### Rules

- Lambdas and anonymous functions are **discouraged as carriers of important application logic**.
- If logic deserves a name, it should become a named function, method, or free function.
- Lambdas are acceptable only when they are:
  - short,
  - local,
  - obvious,
  - free of important domain or workflow logic.
- Do not hide validation, ownership, commit logic, error handling, or domain logic in lambdas.
- Do not build nested lambda chains.
- Capture lists must be minimal and explicit.
- If a lambda needs many captures, multiple conditions, or multiple steps of logic, extract it.
- `capture this` is discouraged unless the lambda is trivial, local, immediately used, and does not escape the current scope.

### Practical rule

Short lambdas for STL, ranges, sorting, filtering, or very small local callbacks are fine.  
Lambdas that start behaving like unnamed workflow objects are not.

## 9. Error handling, assertions, and logging

### Goal

Code must clearly distinguish between:
- a programming error,
- an operational error,
- a diagnostic trace.

### Rules

- Exceptions are allowed at system and library boundaries, but they are **not** the primary model for application-level errors.
- In domain logic, user workflows, parsers, and jobs, prefer an explicit typed error model / result type.
- Assertions are for programming errors and invariant violations.
- **Assertions must not have side effects.**
- Logging is not error handling.
- If an operation may fail in normal program execution, that should be visible in the result type or API contract.
- Do not use `bool` to report failure when the reason matters.
- Errors from external libraries should be caught at the boundary and translated into the project’s own error model.
- Renderer paths follow an exception-free approach.

### Practical rules

- Use `assert` for things that should never happen in correct code.
- Use `Result`, `AppError`, or another structured error type when failure is a real runtime possibility.
- User-facing message and technical context are not always the same thing. Error types should support both.
- Logs should be specific and diagnostically useful, but they cannot replace a clear function contract.
- Do not catch exceptions just to ignore them or collapse them into a useless generic failure.

## 10. Includes, forward declarations, headers, PCH, and templates

### Goal

Headers should stay light, stable, and explicit. Dependencies must be intentional.

### Rules

- A header should include only what it actually needs.
- Prefer forward declarations when the full type definition is not required in the header.
- Do not rely on indirect includes.
- Each file should include what it uses.
- Heavy dependencies should not be placed in headers without a real reason.
- Implementation belongs in `.cpp`, not in `.hpp`, unless there is a technical reason.

### PCH rules

- PCH is a build optimization, not a dependency-hiding mechanism.
- A file must still include what it actually uses, even if it also compiles with PCH.
- Only stable, frequently used, and genuinely heavy headers belong in PCH.
- Do not put narrow, volatile, or accidental dependencies into PCH.
- If the project uses per-module PCH, its contents should reflect real module needs.

### Template rules

- Use templates when they provide a real gain in type safety, reuse, or performance.
- Do not use templates to build clever infrastructure without a real need.
- If a normal function, overload, or simple interface is enough, prefer that.
- Template code should stay small, readable, and local.
- Avoid template-heavy frameworks and elaborate metaprogramming unless they provide clear value.
- Template usage must not obscure ownership, lifetime, or architectural boundaries.

## 11. Concurrency and thread-safety

### Goal

Concurrent code must be predictable, debuggable, and aligned with the project runtime contract.

### Rules

- UI, ImGui, OpenGL, renderer lifecycle, and final commit of live project state are main-thread only.
- Background jobs must not directly mutate live project state. They return results, DTOs, or compute requests for later commit.
- Long-running operations work on snapshots, not on live mutable domain objects.
- Cancellation must be explicit and cooperative.
- Partial commit after cancel or error is not allowed, except for explicit dev/debug workflows.
- Thread affinity should be explicit in critical code paths.
- Use `mutex` / `shared_mutex` only after simpler models such as snapshotting, ownership transfer, or request queues have been exhausted.
- Domain types are **not thread-safe by default**, unless explicitly documented otherwise.
- For volumetric GPU work, follow the request/commit model: background prepares immutable input, render thread executes GPU work, and results are later committed as render resources or derived outputs.

### Practical rules

- If a function is main-thread-only, make that obvious.
- If a type is not thread-safe, do not pretend that it is.
- A mutex is not a substitute for good ownership design.
- If concurrency makes the API hard to reason about, simplify the data flow before adding synchronization.
- Concurrent code should be deliberately boring.

## 12. Use modern C++20/23 well

### Goal

The project should benefit from modern C++ where it improves readability, correctness, safety, and API quality.

### Rules

- Prefer modern C++20/23 style over older patterns when the modern form is clearly better.
- Use newer language and library features when they improve:
  - readability,
  - type safety,
  - correctness,
  - API clarity,
  - maintainability.
- Do not use modern features just because they exist.
- Modern C++ should result in better code, not more impressive code.

### Preferred examples

- `enum class`
- `constexpr`, `consteval`, `constinit`
- `std::optional`
- `std::variant`
- `std::span`
- `std::string_view`
- `std::filesystem`
- `std::ranges`
- `concepts` when they genuinely improve the API contract
- structured bindings
- `if constexpr`
- in-class member initialization
- explicit `override`, `final`, `[[nodiscard]]`, `[[maybe_unused]]`
- `using` instead of old-style `typedef`

### Practical rule

Do not keep outdated patterns out of habit if the language now offers something safer and clearer.

## 13. Testability and reviewability

### Goal

Code should be easy to review, easy to test, and easy to understand after a long pause.

### Rules

- Code should be reviewable in small, logical changes.
- Large changes should be split into smaller coherent steps.
- Functions and classes should have boundaries that make testing possible without bringing up half the application.
- Parsers, serialization, runtime boundaries, and other risky edges should get tests when they are introduced.
- Code should not require hidden tribal knowledge to modify safely.
- Temporary compromises should be visible and scoped.
- If a change cannot reasonably be reviewed in one pass, it is probably too large.
- Public APIs should be more conservative and stable than local implementation code.
- AI-generated code must be reviewed like any other code.

### Practical rules

- Prefer small commits and small PRs that close one logical unit of work.
- Comments should explain **why**, not restate **what** the code already says.
- If implementation intentionally deviates from the desired final shape, say so explicitly.
- Testability is part of API design, not something added at the end.
- Review must check not only “does it work?” but also “does it preserve boundaries, ownership, and clarity?”

## 14. Combined example snippet

```cpp
namespace DefectsStudio::Domain
{
    enum class BuildType
    {
        Debug,
        Release,
        Dist
    };

    struct ThermodynamicsInputSnapshot
    {
        UUID ProjectId;
        UUID AnalysisId;
    };

    struct ThermodynamicsResult
    {
        double FormationEnergyEv = 0.0;
    };

    class AppError
    {
    public:
        static AppError Validation(std::string_view message);
        static AppError Runtime(std::string_view message);
    };

    template<typename TValue, typename TError>
    class Result;
}

class AnalysisRunner
{
public:
    Result<ThermodynamicsResult, AppError> Run(
        const ThermodynamicsInputSnapshot& snapshot) const;

public:
    bool Enabled = true;

private:
    Result<ThermodynamicsResult, AppError> runImpl(
        const ThermodynamicsInputSnapshot& snapshot) const;

private:
    int m_RunCount = 0;
};

// GOOD: short local lambda for sorting
std::ranges::sort(items, [](const auto& a, const auto& b)
{
    return a.Name < b.Name;
});

// BAD: output parameter hides API intent
bool RunAnalysis(const InputData& input, ThermodynamicsResult& outResult);

// GOOD: typed result makes failure explicit
Result<ThermodynamicsResult, AppError> RunAnalysis(const InputData& input);

// BAD: shared ownership used without a reason
Ref<ProjectWorkspace> m_Workspace;

// GOOD: shared ownership only when lifetime is truly shared and long-lived
WeakRef<ProjectWorkspace> m_Workspace;

// BAD: domain type leaking UI/runtime detail
class DefectConfiguration
{
public:
    ImVec4 LabelColor;
};

// BAD: escaped lambda with capture this and too much logic
jobSystem.Enqueue([this]()
{
    auto snapshot = createSnapshot();
    auto result = runImpl(snapshot);
    commitResult(result);
    logResult(result);
});

// GOOD: explicit snapshot and no hidden object capture
auto snapshot = CreateSnapshot();
jobSystem.Enqueue([snapshot = std::move(snapshot)]() mutable
{
    auto result = RunAnalysis(snapshot);
    return result;
});
```

## 15. Final note


The purpose of these guidelines is not to make the codebase rigid.  
The purpose is to keep it understandable, stable, and architecturally consistent during long-term solo development.
