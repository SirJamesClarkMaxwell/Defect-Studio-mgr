# ADR-013 – Commands, Keymap, and Undo for User Actions

Status: Accepted

## Decision

Register user actions in `CommandRegistry`, execute them through `CommandService`, resolve shortcuts
through the input/keymap system, and use `UndoStack` for command-backed shared-state undo.

## Evidence

The registries and service live under `src/Core/Commands`; keymap resolution lives under `src/Core/Input`;
renderer command registration is in `src/Renderer/Commands/RendererCommandRegistration.cpp`.

## Consequences

New user actions should not bypass command registration or keymap resolution.
