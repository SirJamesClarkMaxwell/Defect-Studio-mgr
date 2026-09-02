# Reusable systems

Use the existing entry point before adding a parallel mechanism.

| System | Entry-point path | Use it when | Do not use it for |
|---|---|---|---|
| Event bus | `src/Core/EventSystem/BusEventSystem/EventBus.hpp` | Cross-layer application events. | Platform event dispatch. |
| Dispatching events | `src/Core/EventSystem/DispatchingEventSystem/DispatchingEventSystem.hpp` | Window, keyboard, mouse, or platform input dispatch. | Application state notifications. |
| Commands | `src/Core/Commands/CommandRegistry.hpp`, `CommandService.hpp` | User actions requiring registration/execution/observation. | Background work scheduling. |
| Input | `src/Core/Input/KeymapResolver.hpp`, `ContextManager.hpp` | Key chords, keymaps, contexts, and shortcut resolution. | Direct UI mutation. |
| Undo | `src/Core/Undo/UndoStack.hpp` | Command-backed undo/redo of shared state. | Renderer-local view and label history. |
| Jobs | `src/Core/JobSystem/JobSystem.hpp` | Background work, cancellation, retries, and job events. | Main-thread state commits. |
| Progress | `src/Core/ProgressTrackingSystem/ProgressTracker.hpp` | Progress state for jobs and long operations. | Error reporting. |
| Diagnostics | `src/Core/Diagnostics/StructuredError.hpp` | Structured user-facing failure details and `Result<T>`. | Logging-only messages. |
| Capabilities | `src/Core/Capabilities/CapabilityRegistry.hpp` | Runtime capability checks for gated features. | General configuration. |
| Assets | `src/Core/Assets/AssetManager.hpp` | Logical asset registration and validation. | Project persistence. |
| Notifications | `src/Core/Notifications/NotificationCenter.hpp` | User notifications delivered through the event system. | Exceptions or control flow. |
| Logging | `src/Core/Logging/Logger.hpp`, `LogRegistry.hpp` | Structured logs and runtime diagnostics. | User action routing. |
| Paths | `src/Core/Utils/Path.hpp` | Path normalization and filesystem helpers. | YAML serialization policy. |
| Configuration | `src/App/Managers/ConfigManager.hpp` | Persisted application configuration. | Domain project state. |
| YAML serialization | `src/App/Serialization/YamlCodecFacade.hpp` | YAML config serialization/deserialization. | Arbitrary file formats. |
| Project/renderer I/O | `src/IO` | Loading/saving project, renderer, keymap, and app data. | Domain-to-view conversion. |
| Renderer layer | `src/Renderer/RendererLayer.hpp` | Renderer state changes and viewport actions. | Domain ownership. |
| Presentation | `src/Presentation` | UI composition and runtime UI state. | Cross-layer coordination. |

All listed paths exist at the current HEAD. `Storage` has no comparable persistence implementation:
[`src/Storage/StorageLayer.hpp`](../src/Storage/StorageLayer.hpp) is the only layer type.
