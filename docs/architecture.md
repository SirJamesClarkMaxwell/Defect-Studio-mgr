# Architecture

## Module ownership

| Module | Owns |
|---|---|
| `App` | Application composition, managers, and startup wiring. |
| `Core` | Reusable infrastructure: events, commands, input, jobs, diagnostics, logging, undo, and utilities. |
| `Debug` | Debug-layer integration. |
| `Demo` | Demonstration panels and examples for core systems. |
| `Domain` | Project and scientific structures, defects, and domain operations. |
| `Events` | Application event types shared across layers. |
| `IO` | File formats, manifests, project roots, renderer data, and text I/O. |
| `Presentation` | ImGui UI layers and panels; it collects user intent. |
| `Renderer` | Renderer state, scene/ECS synchronization, viewport commands, and rendering backends. |
| `ScientificRuntime` | Python process/runtime bridges and scientific jobs. |
| `Storage` | The current `StorageLayer`; persistence implementations currently live in `IO`. |

Paths: [`src/App`](../src/App), [`src/Core`](../src/Core), [`src/Domain`](../src/Domain),
[`src/IO`](../src/IO), [`src/Renderer`](../src/Renderer),
[`src/ScientificRuntime`](../src/ScientificRuntime), [`src/Storage`](../src/Storage).

## Observed include direction

Measured from `#include "Module/..."` in `src/**/*.cpp` and `src/**/*.hpp` at HEAD.

| Source | Observed targets |
|---|---|
| `App` | `Core`, `Domain`, `Events`, `IO`, `Presentation`, `Renderer`, `ScientificRuntime`, `Storage` |
| `Debug` | `Core` |
| `Demo` | `Core` |
| `Domain` | `Core` |
| `Events` | `App`, `Core`, `Renderer` |
| `IO` | `App`, `Core`, `Domain`, `Events`, `Renderer`, `ScientificRuntime` |
| `Presentation` | `App`, `Core`, `Domain`, `Events`, `IO`, `Renderer`, `ScientificRuntime` |
| `Renderer` | `Core`, `Domain`, `Events`, `IO` |
| `ScientificRuntime` | `Core`, `Domain` |
| `Storage` | `Core` |

Known violations, not allowed design direction: `Renderer ↔ Events`, `Renderer ↔ IO`, and `IO ↔ App`.
The measured graph also contains other direct includes; do not infer a cleaner layering than the code.

## Boundaries

- `Domain` does not depend on UI, renderer, or `App`.
- `Renderer` may read domain types to build snapshots, but is not domain truth.
- `IO` reads/writes files; view-specific domain-to-renderer transformation belongs in
  `Renderer/StructureRendererDataBuilder`.
- `Presentation` renders UI and collects intent; it must not silently mutate other modules.
- `App` is the composition root, not a domain-logic orchestrator.
- Cross-layer communication uses `EventBus`; user actions use commands and keymap; long work uses jobs and progress.
- Only the main thread commits project/UI-visible state.
- Keep domain structure, renderer scene, UI collection, and on-disk project distinct.

## Source of truth

`ProjectWorkspace` owns `StructureRegistry` in [`src/Domain/ProjectWorkspace.hpp`](../src/Domain/ProjectWorkspace.hpp).
`RendererStructureData` is derived data and carries `domainStructureId` in
[`src/Renderer/RendererTypes.hpp`](../src/Renderer/RendererTypes.hpp); construction is in
[`src/Renderer/StructureRendererDataBuilder.cpp`](../src/Renderer/StructureRendererDataBuilder.cpp).
