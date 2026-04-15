# System Architecture Overview

This is the practical runtime map.

If you are new to the codebase, read this page first and then move to system-specific pages.

## What This System Is

DefectStudio is a modular desktop monolith:

- one executable,
- one process,
- explicit subsystem boundaries.

`Application` is the composition root. It wires systems and runs the frame loop.

## Runtime Flow

Startup and runtime follow this shape:

1. `Application::Create(...)`
2. logger + platform/window + graphics + ImGui initialization
3. default layer stack setup
4. `Application::Run()`
5. main loop:
- poll events,
- update layers,
- render ImGui,
- render frame
6. `Application::Shutdown()`

## Two Event Lanes (Important)

- lane A: local low-level routing (`Event` + `EventDispatcher`)
- lane B: decoupled subsystem messaging (`EventBus`)

Keep those lanes separate. It prevents architecture drift and keeps intent clear.

## Minimal Usage

```cpp
int main(int argc, char** argv)
{
    DefectStudio::Application app(argc, argv);
    return app.Run();
}
```

## Where To Go Next

- Application shell: `Application Shell System`
- Lifecycle details: `Application Lifecycle System`
- Event details (overview): `events/introduction.md`
- Event details (usage): `events/overview.md`
- Event details (implementation): `events/for-programmers.md`
- Background execution: `Job System`
- Progress snapshots: `Progress Tracker System`
- Configuration ownership: `Config Manager System`
