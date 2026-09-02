# Conventions

- Use one branch per task, named `task/NN-short-name` (`CLAUDE.md`).
- Use `<type>(<scope>): <imperative summary>`. The allowed types and scopes are in the archived
  [`commit-convention.md`](archive/work/process/commit-convention.md); the current log uses the same scoped format.
- Keep `.cpp` files around 500 lines; split larger units (`CLAUDE.md`).
- Do not use exceptions on rendering paths. Return `Result<T>` and `StructuredError` instead
  (`src/Core/Diagnostics/StructuredError.hpp`).
- Do not add raw pointers in project code. Use `Ref`, `WeakRef`, `Unique`, references, `std::optional`, or stable IDs.
- Only the main thread commits project/UI-visible state.
- Route cross-layer traffic through `EventBus`.
- Route user actions through `CommandRegistry` + `CommandService` + keymap.
- Route long operations through `JobSystem` + `ProgressTracker`.
- Before a merge to `main`, verify Debug and Release application/test builds and tests.
