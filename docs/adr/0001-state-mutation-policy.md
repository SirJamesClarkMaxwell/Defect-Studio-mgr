% State Mutation Policy (ADR 0001)

## Context
DefectStudio needs a deterministic channel for application state changes so undo/redo, scripting, and async jobs behave consistently.

## Decision
1. All persistent application mutations (project state, `ApplicationConfig`, saved layout) MUST go through the `CommandRegistry`/`ICommand` runtime.
2. Local UI state (panel visibility, selections, scroll) may remain local to UI components. Any mutation that affects `ApplicationConfig` or `Project` is a command.
3. Worker threads MUST NOT touch ImGui or mutable UI state directly. Worker-to-main callbacks use `MainThreadDispatcher`.

## Consequences
- Existing direct mutations (e.g., panels writing `ApplicationConfig` directly) will be audited and migrated.
- `StructuredError` + `Result<T>` will be used for error propagation instead of exceptions in core APIs.
- Undo/Redo semantics require centralization of state mutation.

## Status
Draft — to be reviewed and accepted by maintainers.
