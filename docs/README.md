# Documentation router

| Document | Read it when |
|---|---|
| [architecture.md](architecture.md) | You need module ownership, include direction, or state boundaries. |
| [systems.md](systems.md) | You are adding behavior and need an existing system to reuse. |
| [build-and-test.md](build-and-test.md) | You need to generate, build, or test. |
| [python-runtime.md](python-runtime.md) | You are using a Python bridge or subprocess job. |
| [conventions.md](conventions.md) | You are writing or reviewing code, commits, or cross-layer behavior. |
| [adr/README.md](adr/README.md) | A change needs an architectural decision or you need recorded decisions. |

## Hard rules

- [AGENTS.md](../AGENTS.md) is authoritative for graphify, Ponytail mode, and systems to reuse.
- [CLAUDE.md](../CLAUDE.md) is authoritative for workflow, boundaries, and build-toolchain gotchas.
- ADRs live in [docs/adr](adr/). Historical files in [archive/](archive/) are not authoritative.

When this documentation conflicts with either root policy file, follow the root policy file.
