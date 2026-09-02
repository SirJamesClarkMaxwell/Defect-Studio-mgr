# UI Architecture

The UI layer presents runtime state, tools, and non-blocking diagnostics.

## Current Baseline

Implemented:

- Docking-enabled ImGui shell.
- Basic runtime panel and render loop controls.

## Planned UX Areas

From TODO:

- Outliner and object properties workflow.
- Notification center and history policy.
- Settings hierarchy and persistence.
- Shortcut discovery and conflict validation.
- Rendering and export controls separated from viewport defaults.

## Interaction Policy

UI should expose actionable diagnostics while keeping heavy operations asynchronous.
