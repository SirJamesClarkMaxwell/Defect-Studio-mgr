# Application Lifecycle System

This page explains how the `Application` lifecycle works in DefectStudio after Slice A stabilization.

Think about this system like a tiny runtime state machine that protects startup and shutdown from accidental double calls.

If a desktop app has one place where hidden bugs love to appear, it is lifecycle order:

- creating systems twice,
- shutting down twice,
- or forgetting to shut down in the destructor path.

The goal here is simple: make the lifecycle boring, explicit, and safe.

## Why This System Exists

`Application` already had a working shell, but T04 requires stricter behavior:

- `Create()` must be guarded,
- `Shutdown()` must be idempotent,
- `WindowCloseEvent` must reliably stop the loop,
- destructor path must safely release resources.

To keep this local and testable, a small helper was introduced:

- `ApplicationLifecycleState` in `src/App/ApplicationLifecycle.hpp`.

## High-Level Flow

At runtime, the flow is:

1. `Application` constructor calls `Create(...)`.
2. `Create(...)` marks lifecycle as created (`TryMarkCreated`).
3. Runtime systems are initialized (logger, GLFW, window, GLAD, ImGui, layers).
4. Lifecycle is marked running (`SetRunning(true)`).
5. `Run()` enters loop only when running.
6. `WindowCloseEvent` marks lifecycle as not running.
7. `Shutdown()` performs resource teardown exactly once (`TryBeginShutdown`).
8. `~Application()` calls `Shutdown()` defensively.

## Public API

### `bool Application::Create(const ApplicationSpecification& specification)`

Creates and initializes application runtime systems.

Parameters:

- `specification`: startup options:
  - log level,
  - file logging policy,
  - optional log file path,
  - reset-layout flag,
  - event tracing flag.

Behavior:

- returns `false` when `Create()` is called more than once,
- initializes core subsystems in deterministic order,
- on partial failure, triggers `Shutdown()` to clean up already-created systems.

### `int Application::Run()`

Runs the main loop.

Behavior:

- returns `1` when app is not in running state,
- executes loop while lifecycle is running and window is not closing,
- always calls `Shutdown()` after loop exits.

### `void Application::Shutdown()`

Releases runtime systems in reverse-safe order.

Behavior:

- idempotent: second call is ignored,
- clears `LayerStack`,
- shuts down ImGui,
- destroys window,
- terminates GLFW,
- shuts down logger.

## Internal (Private) API

### `ApplicationLifecycleState`

This is a tiny state holder used by `Application` and tests.

Private state fields:

- `m_Created`: whether create stage has been entered successfully,
- `m_Running`: whether frame loop should continue.

#### `bool TryMarkCreated()`

- First call: sets `m_Created = true`, returns `true`.
- Next calls: returns `false`.

#### `bool TryBeginShutdown()`

- Returns `false` only when app is already fully inactive.
- Otherwise transitions to inactive state:
  - `m_Running = false`,
  - `m_Created = false`.

#### `void SetRunning(bool running)`

- Sets current loop-running flag.

#### `bool IsCreated() const`, `bool IsRunning() const`

- Read-only lifecycle state accessors.

### `void HandleLifecycleEvent(Event& event, ApplicationLifecycleState& state)`

Small event helper for lifecycle-sensitive events.

Parameters:

- `event`: low-level event from `Core/Event.hpp`.
- `state`: lifecycle state object to mutate.

Behavior:

- dispatches `WindowCloseEvent`,
- marks runtime as not running,
- sets `event.handled = true` via dispatcher return value.

## Usage Examples

### Example 1: Standard Entry Point

```cpp
int main(int argc, char** argv)
{
    DefectStudio::Application app(argc, argv);
    return app.Run();
}
```

### Example 2: Idempotent Shutdown Safety

```cpp
DefectStudio::ApplicationLifecycleState state;
state.TryMarkCreated();
state.SetRunning(true);

const bool firstShutdown = state.TryBeginShutdown();   // true
const bool secondShutdown = state.TryBeginShutdown();  // false
```

### Example 3: Stop Loop from Window Close

```cpp
DefectStudio::ApplicationLifecycleState state;
state.TryMarkCreated();
state.SetRunning(true);

DefectStudio::WindowCloseEvent closeEvent;
DefectStudio::HandleLifecycleEvent(closeEvent, state);

// closeEvent.handled == true
// state.IsRunning() == false
```

## Notes for Next Slice

This lifecycle layer is intentionally minimal.

Next slices can safely build on it for:

- stronger loop/event integration tests,
- EventBus main-thread queue boundary,
- startup/shutdown event emission.