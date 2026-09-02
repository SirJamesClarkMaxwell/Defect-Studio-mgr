# Application Shell

## What It Is

`Application` is the composition root and lifecycle controller of the whole runtime.

It is where we wire:

- window,
- render/update loop,
- layer stack,
- core runtime services.

## How It Works

`Create` and `Shutdown` are guarded by lifecycle state, so repeated calls are safe and idempotent.

Runtime services are initialized and destroyed explicitly in dedicated steps:

- `initializeRuntimeServices()` creates service instances in fixed order,
- `shutdownRuntimeServices()` releases them in reverse order.

Configuration IO is explicit too:

- `loadConfigFromPath(path)`
- `saveConfigToPath(path, document)`
- `loadUiSettingsDocument(...)`
- `saveUiSettingsDocument(...)`

The frame loop is intentionally split into small steps:

- poll events,
- update layers,
- ImGui frame,
- render.

## How To Use It

Use one `Application` instance per runtime process.

Architecture rule for this project:

- behavior that changes runtime state must be explicit in code flow,
- avoid hidden side effects in constructors/destructors,
- initialization and shutdown order must be readable in one place.

When extending the shell, keep the file organized by abstraction level: high-level orchestration first, low-level details later.
