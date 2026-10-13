# Task 112b: duplicate in place and move

Implemented Shift+D for atoms and the scene-object kinds supported by the existing modal transform. Ctrl+D keeps its default 0.5 Angstrom offset and its existing cancellation behaviour.

## Files changed

- `src/Presentation/Panels/ViewportInteraction.cpp`: Shift+D starts an undo group, routes duplication through the registered scene shortcut or atom command with a zero offset and an explicit viewport ID, selects only the copies, and starts Translate immediately. The active gesture owns the viewport interaction chain. Failure to start a modal operation rolls the duplicate back.
- `src/Presentation/Panels/ViewportModalTransform.cpp`: confirm closes the duplicate group; Escape/right click restores the preview and cancels the group. Failed atom-transform commits also roll back the duplicate. G/R/S switching applies only while an in-place duplicate is pending and restarts from the copies' original positions, preserving the orientation, bases, pivot, anchor and lattice-unit preference.
- `src/Renderer/Commands/RendererAtomEditCommands.hpp`
- `src/Renderer/Commands/RendererAtomEditCommands.cpp`: the existing duplicate factory/command accepts an offset, defaulting to (0.5, 0, 0). Zero-offset duplication retains the source fractional coordinates exactly instead of recalculating them.
- `src/Renderer/Commands/RendererCommandRegistration.cpp`: reads the optional duplicate offset and window ID from command context; absent values retain the previous behaviour.
- `src/Renderer/RendererWindowState.hpp`: adds `duplicateInPlacePending` beside `duplicateMovePending`.
- `src/Core/CoreLayerShortcuts.cpp`: reserves the conflicting viewport Shift+D binding for the viewport gesture.
- `src/Presentation/Panels/SceneObjectEditActions.hpp`
- `src/Presentation/Panels/SceneObjectEditActions.cpp`: forwards the duplicate offset/window ID through the existing scene shortcut command and retains only the duplicated kind's copy selection for the in-place gesture.
- `src/Presentation/Panels/SceneObjectClipboard.hpp`: optional offset in the existing clipboard-copy/paste helpers, used by pinned-measurement duplication; the default is unchanged.
- `src/Presentation/Panels/ScenePinnedMeasurementOperations.cpp`: forwards the duplicate offset through its existing clipboard round trip.
- `src/Presentation/Panels/SceneOrbitalEditorWidget.hpp`
- `src/Presentation/Panels/SceneOrbitalOperations.cpp`: orbitals and planes accept the offset while retaining the existing detachment behaviour.
- `src/Presentation/Panels/ScenePathOperations.hpp`
- `src/Presentation/Panels/ScenePathOperations.cpp`: paths pass the requested offset to their existing detach/copy helper. Ordinary paste retains its original offset.
- `tests/Renderer/RendererAtomDeleteCommandTests.cpp`: adds a duplicate fixture using the existing headless domain/renderer setup and four tests; all existing fixture/test text is preserved.
- `docs/work/project/reports/task-112b-duplicate-in-place.md`: this report.

Scene hide volumes and densities already duplicate without an offset, so their duplicate implementations were not changed. Routing retains Ctrl+D's precedence: atom/bond selection wins; otherwise the first selected scene kind wins. Other original selections are removed from the Shift+D transform targets so the gesture moves copies only.

## Grouping and cancellation

No compound command or undo mechanism was added. The existing `UndoStack::BeginGroup` is called before duplication. CommandRegistry pushes the executed atom duplicate, or the existing scene snapshot command, into that group. Modal confirmation adds the normal transform command/snapshot and calls `EndGroup`, producing one history record.

Escape/right click restores the renderer preview and calls the existing `CancelGroup`, which invokes the accumulated commands' Undo in reverse order and discards the group. It does not create a redo record or prune the pre-existing redo branch. Cancellation therefore restores the atom list and history depths from before the gesture. An unavailable command or a selection without modal targets takes the same rollback path. Undo errors are logged through the existing diagnostics/logging pattern.

Ctrl+D does not set the new flag or open this group; its shortcut and next-frame modal-start code are unchanged.

## Tests added

1. `ZeroOffsetPreservesCartesianAndFractionalPositionsAndSelectsCopies`: non-orthogonal periodic cell, exact source/copy Cartesian and fractional coordinates, and selected copy indices.
2. `DefaultOffsetDuplicateKeepsItsOwnUndoEntry`: factory defaults still produce the 0.5 Angstrom nudge and an independent undo record.
3. `DuplicateAndTransformGroupUndoAndRedoAsOneStep`: a committed group produces one undo record; undo restores the original atoms/selection and redo restores both moved copies.
4. `CancelDuplicatePreservesAtomsHistoryAndExistingRedoBranch`: cancellation removes copies and preview movement, preserves existing undo/redo depths and clean state, and leaves the prior redo operation usable.

## Validation

- Native MinGW GCC 13.2 C++23 syntax checks passed for every changed C++ translation unit, including the new tests, against the generated project's include paths/platform definitions.
- **4/4 new command tests passed** in a focused temporary native harness. The harness compiles the unchanged production duplicate/transform class bodies, factories and atom-target/rebuild helpers, real domain/coordinate/bond code, real ECS atom/bond synchronization and real UndoStack. Only the renderer window container and logging are isolated; the unrelated vacancy-label setup is omitted. Production bodies and test cases are extracted into temporary files, not rewritten implementations. This is focused command validation, not the full application-linked test target.
- **34/34 modal-transform tests passed** by rerunning the existing task-112a native executable. Its source/test files were not changed by this task.
- Scoped `git diff --check` passed. Existing atom-edit fixture/test text matches HEAD exactly.
- The previous 112a fraction entry, units toggle, overlay and invalid-number confirmation guard remain in ViewportModalTransform.cpp. ModalTransform.cpp, ModalTransform.hpp and ModalTransformTests.cpp retain their starting SHA-256 hashes.
- No new repository source/test files were added, so project regeneration was unnecessary. All test outputs/adapters were written under the system temporary directory.

## Key conflict

`install/users/default/config/keybindings.yaml` already binds Shift+D in `renderer.viewport.focused` to `renderer.view.set_as_project_default`. The keymap dispatcher consumes that particular binding so the viewport handles Shift+D without also changing the project default view. The view command remains registered and available from the command palette. The install keymap was not edited, as the task excludes install/.

## Anything not done / limits

- Density objects have no G/R/S target representation in the existing SceneTransform system. A density-only Shift+D is rolled back when modal start finds no targets; it leaves no coincident copy or dangling history entry. Adding density transform support would require files outside the task's allowed list, so that part of the requested scene-object coverage is not implemented.
- No interactive viewport check, full solution build, MSVC Debug/Release build, full application-linked test run, or commit. Scene-object duplication and G/R/S key handling passed syntax checks but were not exercised interactively.
- Graphify was queried before source navigation. Graph regeneration was deferred because `graphify update .` writes graphify-out/, outside the user's explicit allowed-file scope.
- The pre-existing oversized RendererAtomEditCommands.cpp and RendererCommandRegistration.cpp were not split, since that would require extra source files and unrelated changes. Other changed .cpp files remain below 500 lines.
- No Phonons, EditorLayer, docking, other documentation, Domain, IO, Vendor, install or build files were edited. Existing uncommitted work was preserved.
