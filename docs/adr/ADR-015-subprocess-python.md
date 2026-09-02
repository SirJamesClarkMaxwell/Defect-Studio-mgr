# ADR-015 – Subprocess-Only Python in the Current Build

Status: Accepted

## Decision

Keep `DS_PYTHON_CAPI_AVAILABLE=0` for the current Debug and Release builds. Python bridges execute
through `ScriptRunner` and the platform process runner.

## Consequences

Each call has process and import startup cost. Cancellation and timeout must use `ProcessRunOptions`;
the two capability-dependent tests remain skipped.
