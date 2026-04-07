# Python Bridge Architecture

Python is treated as Scientific Runtime infrastructure, not as the primary end-user scripting UX for MVP.

## Intended Responsibilities

- Manage interpreter lifecycle safely.
- Expose selected DTO and service adapters.
- Integrate scientific packages through capability-gated ports.

## Planned Components

From TODO:

- PythonInterpreter RAII wrapper.
- Bridge adapters for pymatgen and ASE.
- GIL-safe usage inside background jobs.
- Capability and version reporting for scientific dependencies.
- Structured error conversion into application-level diagnostics.

## Contract Direction

Bridge APIs should stay narrow and typed:

- explicit DTO inputs and outputs,
- adapter contract tests,
- no hidden side effects in domain state.
