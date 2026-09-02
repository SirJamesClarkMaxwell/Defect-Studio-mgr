# LayerStack System

## What This System Is

`LayerStack` is the ordered runtime pipeline for feature logic and UI participation.

Each layer uses the same contract:

- `OnAttach`
- `OnDetach`
- `OnUpdate`
- `OnEvent`
- `OnImGuiRender`

## Regular Layer vs Overlay

Regular layer:

- part of the normal feature pipeline,
- typically used for runtime modules and editor features,
- examples: IO, Storage, ScientificRuntime, Domain, Editor.

Overlay:

- sits above regular layers,
- should be used for privileged or top-level tools,
- examples: debug diagnostics, top-level developer panels.

Why this distinction matters:

- updates run forward through the stack,
- events run in reverse,
- overlays can intercept events before lower layers.

## How It Works

- `PushLayer(...)`: add to regular layer range.
- `PushOverlay(...)`: add to overlay range.
- `PopLayer(...)` / `PopOverlay(...)`: remove and detach.

## Simple Usage

```cpp
m_LayerStack.PushLayer(DefectStudio::CreateUnique<MyFeatureLayer>());
m_LayerStack.PushOverlay(DefectStudio::CreateUnique<MyDebugOverlay>());
```

## Practical Rule

If it is core feature behavior, prefer a regular layer.

If it is privileged instrumentation or top-level diagnostics, prefer an overlay.
