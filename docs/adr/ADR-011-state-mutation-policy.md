# ADR-011 – State Mutation Policy

## Context
DefectStudio needs a deterministic channel for application state changes so undo/redo, scripting, and async jobs behave consistently.

## Decision
1. All persistent application mutations (project state, `ApplicationConfig`, saved layout) MUST go through the `CommandRegistry`/`ICommand` runtime.
2. Local UI state (panel visibility, selections, scroll) may remain local to UI components. Any mutation that affects `ApplicationConfig` or `Project` is a command.
3. Worker threads MUST NOT touch ImGui or mutable UI state directly. Errors and results from worker threads are posted to the main thread and presented through `Notifier` (non-blocking) or blocking popups (pre-execution validation failures only).

## Consequences
- Existing direct mutations (e.g., panels writing `ApplicationConfig` directly) will be audited and migrated.
- `StructuredError` + `Result<T>` will be used for error propagation instead of exceptions in core APIs.
- Undo/Redo semantics require centralization of state mutation.

## Status
Accepted

The renderer's `RendererWindowState::viewUndoHistory` and `viewRedoHistory` are deliberately
per-window local UI state and remain outside the global `UndoStack`.

## Verified 2026-09-02 (HEAD 817ae09ed6f169825a992ea1960af3fcea682fa7)
Partially holds.
`RendererLayer` owns view undo/redo and command registration routes user actions through commands,
but the decision's claim that all persistent mutations already use that runtime needs continued review.
