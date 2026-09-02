# Layers Architecture

Layers define presentation and runtime composition order inside the application loop.

## Planned Layer Roles

From TODO:

- IOLayer for file and external integration handling.
- StorageLayer for project/session persistence ownership.
- ScientificRuntimeLayer for scientific Python-backed capability adapters.
- DomainLayer for runtime domain orchestration.
- ImGuiLayer for UI context and docking lifecycle.
- EditorLayer for domain editing workflow and tools.
- DebugLayer for privileged diagnostics and developer-only panels.

## Design Goals

- Predictable push and pop and overlay semantics.
- Minimal coupling between panels through event bus messaging.
- Main-thread-only commits for project-visible runtime state.
