# ADR-012 – Result and StructuredError as the Error Contract

Status: Accepted

## Decision

Use `Result<T>` / `Result<void>` carrying `StructuredError` for fallible project operations.
Do not introduce exceptions as the rendering-path error contract.

## Evidence

`StructuredError.hpp` defines both result forms; command, undo, domain, IO, and Python code return or consume them.

## Consequences

Callers must inspect errors and preserve structured diagnostics for UI or logs.
