# Task 48a: Path Edit Mode keys go through CommandRegistry + keymap

Closes PathSystem stage S13 (`docs/work/project/plans/2026-09-20-path-system-implementation.md`),
rule C12 of the v2 plan: every user-visible path operation goes through `CommandRegistry` +
`CommandService` + keymap.

## Goal

Today `ViewportScenePathInteraction.cpp` polls Tab, Escape, 1/2/3, E, Delete and V with raw
`ImGui::IsKeyPressed`. The keymap also binds E (roll_right), V (view.cycle_next), Delete
(selection.delete) and 1/2/3 (align_axis_a/b/c) in the viewport, so in Edit Mode one key press fires
both: pressing 1 switches to node mode AND snaps the camera to axis a. After this task those keys are
registered commands with keybindings that apply only while a path is open in Edit Mode, the raw
polling is gone, and one press fires exactly one action.

## The contract, already written

- `src/Presentation/Panels/ScenePathEditCommands.hpp` - command ids, default chords, the
  focused-window rule, the modal-transform rule, the context name. **Read every comment.**
- `src/Renderer/RendererWindowState.hpp` - new field `pathHandleTypeMenuRequested`.
- `tests/Presentation/Panels/ScenePathEditCommandsTests.cpp` - the commands.
- `tests/IO/KeyBindingIoEventsTests.cpp`, test `PathEditBindingsWinOnlyWhileEditModeIsActive` -
  the shipped keymap.

## What already exists - reuse it

- `src/Presentation/Panels/ScenePathOperations.{hpp,cpp}` - `ExtendSelectedScenePathEnd`,
  `InsertSelectedScenePathSegment`, `DeleteSelectedScenePathNodes`, `SetSelectedScenePathHandleType`,
  `ReverseEditedScenePath`. Every edit command is a thin call to one of these. Do not re-implement
  any topology, selection repair or undo.
- `src/Presentation/Panels/SceneArrowOperations.cpp` - `ReverseSelectedSceneArrowsCommand` shows the
  `ICommand` shape and the focused-window lookup (focused id, or the only window when none is
  focused). `src/Presentation/Panels/ViewportLabelInteraction.cpp`
  `RegisterViewportSceneObjectCommands` shows registration with `CommandMeta`.
- `src/Presentation/Panels/ViewportInput.cpp` `UpdateViewportFocusState` shows how a context is set
  on `ContextManager`.
- `PathEditSession` (`src/Renderer/Path/PathEditSession.hpp`) - Enter / Leave / SetElementMode.

## Files to create or change

- `src/Presentation/Panels/ScenePathEditCommands.cpp` - **new**: the commands, registration,
  `UpdateScenePathEditContext`.
- `src/Presentation/Panels/RendererPanel.cpp` - call `RegisterScenePathEditCommands` next to
  `RegisterViewportSceneObjectCommands`; call `UpdateScenePathEditContext` once per frame (the panel
  already holds `m_ContextManager` as a `WeakRef`).
- `src/Presentation/Panels/ViewportScenePathInteraction.cpp` - delete the raw Tab / Escape / 1 / 2 / 3
  / E / Delete / V polling. Keep the handle-type popup itself; open it when
  `windowState.pathHandleTypeMenuRequested` is set, then clear the flag.
- `install/users/default/config/keybindings.yaml` - append the bindings **at the end of the file**
  (registration order decides between same-layer bindings on one chord; later wins), all
  `layer: 2`:
  - `Tab` -> `renderer.path_edit.toggle`, context `renderer.viewport.focused`
  - `Escape`, `1`, `2`, `3`, `E`, `Delete`, `V` -> the matching command, context
    `renderer.viewport.focused && renderer.path_edit.active`
  - insert, reverse and the four handle types get **no** chord (Alt+R is the legacy arrow reverse and
    an existing test pins it to one binding). They stay reachable from the command palette and the
    Properties panel buttons.
- `docs/work/project/plans/2026-09-20-path-system-implementation.md` - one line under S13 saying the
  keys now route through the keymap (task 48a).

Then regenerate projects: `scripts/Windows/GenerateProjects.bat`.

## Files that must NOT be touched

- `src/Presentation/Panels/ScenePathEditCommands.hpp`, the two test files above (the contract).
- `src/Renderer/Path/**`, `src/Renderer/Scene/**` - the path layer is done; this task only routes keys.
- `src/Presentation/Panels/ScenePathOperations.{hpp,cpp}`.
- `src/Presentation/Panels/ViewportModalTransform.cpp` and everything bevel-related
  (`PathSolidMesher*`, `PathDecorationMesher*`, `*Bevel*`).
- `src/Core/Input/**` - the resolver and context grammar already support what is needed.
- Any other entry in `keybindings.yaml` (append only).

## Acceptance criteria

1. Release build of DefectStudio and DefectStudioTests succeeds.
2. `DefectStudioTests --gtest_filter=ScenePathEditCommandsTests.*:KeyBindingIoEventsTests.*` passes.
3. The full suite passes with only the two known skips
   (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
   `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`).
4. `grep -n "ImGuiKey_\(Tab\|Escape\|1\|2\|3\|E\|Delete\|V\)" src/Presentation/Panels/ViewportScenePathInteraction.cpp`
   finds nothing.
5. New `.cpp` under ~500 lines.

## Constraints

- Presentation layer only; no new dependency from `Renderer` or `Domain` on `Presentation`.
- Commands mutate state only through `ScenePathOperations` / `PathEditSession` and run on the main
  thread like every other command.
- No exceptions; errors are the `StructuredError` the operation returned.
