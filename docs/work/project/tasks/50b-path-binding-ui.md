# Task 50b: bind a path node to atoms from Edit Mode

Second slice of PathSystem stage S14. Task 50a made bound nodes render, pick and transform at their
targets; this one lets the user create, edit and remove bindings.

## Goal

In Path Edit Mode, with one node active, the Properties panel shows that node's binding and offers:
bind to the selected atom(s), edit offset / buffer, and detach without the node jumping. G/R/S on a
bound node moves its binding offset, so the node follows the cursor and stays bound (the v2 plan's
"explicit detach-vs-edit-offset choice": moving edits the offset, Detach is the explicit button).

Workflow the UI must support: click an atom (or Ctrl-click two), click the path, Tab, click a node,
press "Bind to selected atom(s)". Clicking a path does not clear the atom selection today - keep it
that way.

## The contract, already written

- `src/Presentation/Panels/ScenePathBindingOperations.hpp` - **new**; the four operations and every
  error code. **Read every comment.**
- `src/Renderer/Scene/SceneTransform.hpp` - `PathElementTransformStart` gains `bound` and
  `bindingOffset`.
- `tests/Presentation/Panels/ScenePathBindingOperationsTests.cpp`.

## What already exists - reuse it

- `SetScenePathBinding` (`src/Renderer/Path/PathCommands.hpp`) - the one mutation; one undo entry,
  atomic. Every operation here goes through it (Detach writes position AND binding in one
  `ApplyPathEdit` call, not two commands - one undo entry).
- `MakeWindowPathEditContext` (`ScenePathOperations.hpp`) - the window-bound edit context.
- `SceneSystem::MakePathBindingContext` + `ResolveNodePositions` - the resolved world position for
  Detach.
- `PathTransform` inverse: the same local<->world arithmetic `SceneTransformPathElements.cpp`
  already does for translations; reuse its helper rather than re-deriving it.
- `src/Presentation/Panels/ObjectPropertiesPanelPathSection.cpp` `DrawPathEditActions` - the Edit
  Mode section of the panel; the binding UI goes there, under the arc editor's pattern
  (`ImGuiInputTextFlags_EnterReturnsTrue` commits, `ReportPathEditResult` on failure).

## Files to create or change

- `src/Presentation/Panels/ScenePathBindingOperations.cpp` - **new**.
- `src/Presentation/Panels/ObjectPropertiesPanelPathSection.cpp` - a "Binding" sub-section shown
  when the active element is a node:
  - kind line: `Free` / `Atom #i (El)` / `Bond midpoint #i-#j` / `Object origin`, plus a warning line
    when the resolver reports the binding broken (diagnostics from `ResolveNodePositions` with the
    live context);
  - button "Bind to selected atom(s)" - disabled with a tooltip when the selected-atom count is not
    1 or 2;
  - for a bound node: offset X/Y/Z (commit on Enter) and, only for an endpoint CopyPosition, buffer
    (commit on Enter);
  - button "Detach (keep position)".
  Keep the file under ~500 lines; if it would pass that, put the binding section in its own
  `ObjectPropertiesPanelPathBindingSection.cpp` with a declaration in the existing sections header.
- `src/Renderer/Scene/SceneTransformPathElements.cpp` - capture fills `bound` / `bindingOffset`; apply
  on a bound node changes `binding.offset` (world space - a bound node is untransformed, see the
  PathBindingResolver.hpp comment) by the world displacement the node would have had, and leaves
  `position` alone; restore puts the offset back. Rotate/Scale: compute the node's new WORLD position
  exactly as for a free node, then offset += newWorld - startWorld. Handles keep their current rules.
  Update the header comment's element rules with one bullet for bound nodes.

Then regenerate projects (new files): `DS_TOOLSET=msc-v143` + `scripts\Windows\GenerateProjects.bat`.

## Files that must NOT be touched

- The contract header, the `SceneTransform.hpp` contract fields, the test file.
- `src/Renderer/Path/**` (resolver, commands, topology are done), `ScenePathPersistence.*`,
  `SceneObjectsIO.*`.
- Everything bevel-related (`PathSolidMesher*`, `PathDecorationMesher*`, `PathStrokeMesher*`,
  `*Bevel*`).
- `ScenePathEditCommands.*`, `ScenePathOperations.*` (call them, do not change them).

## Acceptance criteria

1. Release build of DefectStudio and DefectStudioTests succeeds.
2. `DefectStudioTests --gtest_filter=ScenePathBindingOperationsTests.*:ScenePathOperationsTests.*:ScenePathUndoTests.*`
   passes.
3. Full suite: no new failures (known: 5 `PathStrokeMesherTests` bevel failures, 1 skip).

## Constraints

- Presentation calls Renderer, never the reverse.
- ObjectOrigin is deliberately not offered (persistence cannot reload it yet - see the header).
- No exceptions; errors as `StructuredError` with the codes in the header.
