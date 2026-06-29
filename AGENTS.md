## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, use the installed graphify skill or instructions before doing anything else.

Rules:
- Before any repository search or code navigation, query graphify first when graphify-out/graph.json exists. Use `rg`, direct file reads, or broad source browsing only after graphify returns insufficient context or when the user explicitly asks to skip graphify.
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).

## Ponytail

Use Ponytail mode in this repository: prefer the smallest correct change after understanding the real flow.

Rules:
- First ask whether the work needs to exist at all; if not, say so.
- Reuse existing codebase helpers and patterns before writing new code.
- Prefer the standard library, native platform features, and already-installed dependencies over new abstractions or dependencies.
- Keep diffs short and boring; do not add boilerplate or abstractions unless explicitly requested or clearly necessary.
- Fix bugs at the shared root cause, not only at the named symptom.
- Do not cut validation, data-loss handling, security, accessibility, or required checks.
- Mark intentional simplifications with a `ponytail:` comment only when the shortcut has a known ceiling and an upgrade path.

## Implemented Systems To Reuse

Before adding a new mechanism, check whether one of these existing systems already covers the work:

- `Core/EventSystem/BusEventSystem`: cross-layer application events and subscriptions.
- `Core/EventSystem/DispatchingEventSystem`: platform/window/input event dispatch.
- `Core/Commands`: command registry, command execution, command observers, undo integration.
- `Core/Input`: key chords, keymaps, contexts, input backend, shortcut resolution.
- `Core/Undo`: undo/redo stack for command-backed changes.
- `Core/JobSystem`: background jobs, cancellation, retries, priority, job events.
- `Core/ProgressTrackingSystem`: progress state for jobs and long-running work.
- `Core/Diagnostics`: structured errors and user-facing failure details.
- `Core/Capabilities`: runtime capability checks for gated commands/features.
- `Core/Assets`: logical asset registration and validation.
- `Core/Notifications`: user notifications through the event bus.
- `Core/Logging`: logging, log registry, runtime diagnostics.
- `Core/Utils/Path` and filesystem helpers: path normalization and file access utilities.
- `App/Managers/ConfigManager`: persisted application configuration.
- `App/Serialization`: YAML config serialization/deserialization.
- `IO`: loading/saving project, renderer, keymap, and app data.
- `Renderer` events and `RendererLayer`: renderer state changes and viewport actions.
- `Presentation` panels and `EditorLayer`: UI composition and runtime UI state.
