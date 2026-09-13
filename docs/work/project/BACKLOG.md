# Backlog

Open items found during manual testing that are not yet scheduled into a task.

- **Undo after a small atom move looks slightly off** (2026-09-13, manual run of the hide/delete fix). Hard to
  describe; it worked when repeated. Reproduce with Object Properties open (coordinates visible): move an atom
  a tiny amount with the gizmo / G, then Ctrl+Z, and compare coordinates before/after. Suspects: float
  round-trip of Cartesian vs fractional positions in the transform command's undo, or a gizmo drag
  producing several commands.
- **Python example scripts are found relative to the working directory, not the executable**
  (`ResolvePythonExampleScript`, `ScientificRuntime/Python/ScriptBridgeUtils.cpp`). An exe built in one
  checkout but started from another repo's directory silently uses that repo's scripts ("Python script
  file was not found" for scripts that only exist in the exe's own checkout). Also search upward from the
  executable's directory. *Fixed on task/23-group-theory-panel (executable directory searched first).*
