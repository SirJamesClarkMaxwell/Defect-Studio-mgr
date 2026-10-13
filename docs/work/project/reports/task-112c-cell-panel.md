# Task 112c: cell panel, repeat and wrap

Implemented the binding Domain helper signatures, one undoable structure-replacement command, and the `Komórka` panel. Presentation computes replacement structures through the pure Domain helpers and sends them through CommandRegistry; it never writes the live domain structure.

## Files changed

- New `src/Domain/Crystal/CellEditing.hpp` and `.cpp`: `CellEditPositions`, `WithCell`, `CellFromParameters`, `ParametersOf`, and `WrapIntoCell`. Cell edits keep fractional or Cartesian coordinates consistently for atoms and vacancies, and move the defect-frame origin according to that policy. Parameter reconstruction preserves the current orientation, handedness, and unchanged a/b vectors. Invalid, singular and non-finite cells are rejected. Wrap retains every site and reports sorted, unique coincident atom indices through the existing `FindCoincidentAtomIndices`.
- New `src/Renderer/Commands/RendererCellEditCommand.hpp` and `.cpp`: `ReplaceStructurePayload` and `CreateReplaceStructureCommand`. The command captures the complete original CrystalStructure, regenerates auto bonds once, marks the structure file dirty, and reuses `RebuildAndSync` for every open window sharing the domain record. Execute/undo/redo clear selection through `ClearAllSceneSelection`. Undo restores the original cell, atoms, vacancies, defect frame, bonds, settings and metadata; redo replays the saved replacement. A reused window with a different structure is rejected. Existing scene/hidden-state snapshots restore vacancy annotations, visibility and defect-frame orientation on undo.
- `src/Renderer/Commands/RendererCommandRegistration.cpp`: registers `renderer.cell.replace_structure` with the existing factory/payload pattern. Only 19 added lines; existing formatting and code are preserved.
- New `src/Presentation/Panels/CellPanel.hpp` and `.cpp`: borderless 2x3 length/angle DragFloat grid with captions above, c/a and volume readouts, fractional/Cartesian policy radios, 1..10 n/m/k inputs and in-place repeat, wrap and coincidence count. Cell edits commit on deactivation after editing, once per gesture. A separate unsnapped drag buffer lets Ctrl snap angles to 60/90/120 without trapping the drag at one angle; text input is detected with `TempInputIsActive` and remains unsnapped. Fields refresh after external changes/undo and target changes. Controls have tooltips; missing/non-periodic structures and unavailable editing show reasons. Active modal transforms disable cell actions.
- `src/Presentation/EditorLayerPanels.cpp`: includes and registers CellPanel alongside the structure panels, making it available in the View menu's Structure category. Only 3 added lines; existing formatting and code are preserved.
- New `tests/Domain/Crystal/CellEditingTests.cpp` and `tests/Renderer/RendererCellEditCommandTests.cpp`.
- `docs/work/project/reports/task-112c-cell-panel.md`: this report.

## Tests added

Seven Domain GoogleTests:

1. Zincblende a=5.43 to c=2a with both position policies, unchanged fractional/Cartesian coordinates as appropriate, and consistent conversions.
2. Singular and non-finite cell rejection, non-finite site rejection, and unchanged input.
3. Vacancy metadata and defect-frame origin follow both position policies; frame axes remain unchanged.
4. Rotated hexagonal/triclinic and left-handed parameter round trips within 1e-4; changing only c preserves a/b exactly.
5. Invalid lengths, angles, incompatible angle triples and NaN parameters are rejected.
6. Required (1.25, -0.5, 0.999999) wrap, both coincident atoms retained/reported, vacancy wrap and metadata preservation, tiny-negative boundary handling, unchanged defect origin.
7. A previous coincidence report is replaced by an empty report for distinct sites.

Four command GoogleTests, using the actual renderer/domain setup, registered command and UndoStack:

1. Repeat 1x1x2: 8 to 16 atoms, c doubled, one undo entry, generated bonds, dirty flag, both windows rebuilt, selection cleared, vacancies/frame dropped; undo restores exact original structure and vacancy labels/orientation; redo restores the 16-atom result.
2. Cell edit and wrap both use the same command; undo restores the original structure or unwrapped coordinates, and redo reapplies wrapping.
3. Invalid/non-periodic replacements do not mutate the structure, revision, dirty flag or history.
4. Undo rejects a window reused for another domain structure and preserves that structure/history.

## Repeat: vacancies and defect frame

Repeat calls the existing `BuildSupercell(structure, SupercellMatrix::Diagonal(n, m, k))` unchanged. It builds a new CrystalStructure, carries periodicity and bond settings, copies atom metadata into replicated sites, assigns fresh indices and adds ` supercell` to the domain name. It does **not** carry vacancies, the defect frame or existing bonds. The command regenerates bonds afterwards. The panel tooltip explicitly says vacancies and the defect frame are removed by repeat and restored by undo.

Dropped vacancy references use the existing vacancy-removal helper before rebuilding: ordinary vacancy labels become free labels, and generated vacancy-bond paths follow the helper's removal behaviour. Scene snapshots restore them on undo. Repeat does not introduce an old-to-new atom-index mapping for atom-bound annotations; existing atom bindings continue to use BuildSupercell's output ordering. Its replication/search algorithm is unchanged.

## Validation

- Native MinGW GCC C++23 `-fsyntax-only` checks passed for all new production/test translation units, RendererCommandRegistration.cpp and EditorLayerPanels.cpp, using the generated include paths and platform definitions. This is syntax validation, not a build or executed test result.
- Scoped `git diff --check` passed. Existing-file diffs contain additions only; no blank lines, conditions or braces were removed/reformatted. New production .cpp files are below 500 lines. The pre-existing oversized RendererCommandRegistration.cpp was kept intact rather than split outside this task.
- Ran the required project-generation command with `DS_TOOLSET=msc-v143`. Premake successfully regenerated both application/test projects and filters; verified that all new sources and test files are included. The wrapper's git-submodule step emitted the sandbox's known MSYS `CreateFileMapping` failure; the subsequent project generation completed successfully (exit 0). Generated build project files were updated only by this explicitly requested command.
- Context7 is unavailable in this session. ImGui deactivation, tooltip and temporary-input APIs were checked against the [official public header](https://raw.githubusercontent.com/ocornut/imgui/master/imgui.h) and [official internal header](https://raw.githubusercontent.com/ocornut/imgui/master/imgui_internal.h); syntax checks also passed against the repository's installed headers.
- Graphify queries were used before source navigation. No other run's Phonons/session/IO changes or user configuration changes were edited or reverted.

## Anything not done

- No MSBuild, linked application build, GoogleTest execution, interactive UI check or commit; the user will build and test.
- Graph regeneration was deferred because `graphify update .` writes graphify-out/, outside this task's explicit allowed-file scope.
