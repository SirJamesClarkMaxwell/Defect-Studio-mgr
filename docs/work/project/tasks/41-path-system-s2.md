# Task 41 S2: path topology and handle rules (pure)

Design contract: `docs/work/project/plans/2026-09-20-path-system-redesign-v2.md` section 3
("Topology contract") - read it, it is short and it is binding. Stage plan:
`docs/work/project/plans/2026-09-20-path-system-implementation.md`, section "S2 - topology (pure)".
S1 (model, evaluator, binding resolver) is already merged on this branch and is the foundation.

## Goal

Structural editing of a path, still with no renderer, UI or GL dependency: split a segment without
moving the curve, extend an end, delete a node, reverse a path, and make a corner tangent. Every
operation is atomic - a rejected operation leaves the `ScenePath` byte-identical, because the
operation runs on a working copy and commits only on success.

## Files to create or change

Create (implementation):
- `src/Renderer/Path/PathTopology.cpp`
- `src/Renderer/Path/PathHandleRules.cpp`

Create (tests):
- `tests/Renderer/Path/PathTopologyTests.cpp`
- `tests/Renderer/Path/PathHandleRulesTests.cpp`

Already written, THE CONTRACT, do not change:
- `src/Renderer/Path/PathTopology.hpp`
- `src/Renderer/Path/PathHandleRules.hpp`

Read-only foundation from S1 (do not edit):
- `src/Renderer/Path/PathTypes.hpp`, `PathEvaluator.{hpp,cpp}`, `PathBindingResolver.{hpp,cpp}`

## Files that must NOT be touched

- Every existing `.cpp`/`.hpp` outside `src/Renderer/Path/` and `tests/Renderer/Path/`.
- The S1 files listed above, and the two S2 headers. If a signature looks wrong, STOP and say so in
  the final report instead of changing it.
- `premake5.lua`, `.vcxproj` files.

## Acceptance criteria

Each is one or more GoogleTest `TEST`. Names are yours; coverage is not.

1. `InsertNode` on a Line: exact split, the new node sits at the lerped position, both halves are
   Lines, and re-evaluating the path over a 64-point sweep of the original parameter reproduces the
   original positions within `1e-6` (see the tolerance note below).
2. `InsertNode` on a Cubic: de Casteljau split (four new handle positions), same 64-point curve
   equality within `1e-6`; handle types of the split halves are consistent with the original.
3. `InsertNode` on an Arc: sweep split `t*theta` / `(1-t)*theta` in the same plane, same 64-point
   curve equality within `1e-6`; the derived radius of both halves matches the original within `1e-6`
   relative.
4. `InsertNode` rejects: bad segment index, `t` outside `(0, 1)`, non-finite `t` - path unchanged.
5. `ExtendEnd` at either end inherits the terminal segment's type: Line -> Line, Cubic -> Cubic with
   Vector handles, Arc -> Arc with the same normal and sweep. A single-node path extends with a Line.
   The existing nodes keep their ids and positions.
6. `DeleteNode` matrix: interior node between two Lines merges into one Line (node count -1, segment
   count -1); interior node between Line and Arc (or any non-Line pair) is rejected with a
   `StructuredError` and the path is byte-identical; deleting an endpoint removes its adjacent
   segment; deleting the last remaining node is rejected; an unknown `PathElementId` is rejected.
7. `ReversePath`: node and segment order reversed, cubic handles swapped, arc sweeps negated. Applying
   it twice is the identity on every field (nodes, ids, handles, handle types, sweeps, normals).
8. `ReversePath` preserves geometry: the reversed path evaluated at `1 - t` gives the same positions as
   the original at `t` within `1e-6`, and the tangent is the negated original tangent.
9. `MakeTangent` on Cubic-Cubic: the two handles around the node become opposite rays along
   `normalize(next - prev)`, each with length `1/3` of its adjacent chord.
10. `MakeTangent` on Cubic-Line and Cubic-Arc: the cubic handle aligns to the rigid neighbour's end
    tangent (compare against `EvaluateSegment`'s tangent at that end), and the rigid segment keeps its
    type and data.
11. `MakeTangent` on Line-Arc / Line-Line / Arc-Arc: rejected with a `StructuredError`, path unchanged.
    A degenerate configuration (coincident neighbours, zero chord) is also rejected, never guessed.
12. `ApplyAutoHandles`: handles typed `Auto` are re-derived by the same rules; `Free`, `Aligned` and
    `Vector` handles are untouched; no segment changes type.
13. Atomicity: after every rejected operation above, the `ScenePath` compares equal to a copy taken
    before the call (nodes, segments, ids, `nextElementId`).
14. New element ids come from `AllocateElementId` - monotonic, never reused within the path.
15. Existing tests, including everything from S1 and `SceneArrowGeometryTests`, still pass untouched.

### Tolerance note

The stage plan says `1e-9`; that number predates the float-storage decision and is wrong. A split
writes the new node position back as `glm::vec3`, so every split tolerance is float-sourced: roughly
`1e-7` relative, which is `1e-6` absolute at the coordinate magnitudes these tests use (plan v2 C9,
same reason `PathEvaluatorTests` uses `1e-7` for the arc radius). Use `1e-6`. Where nothing is
re-stored - `Reverse` twice, id and type comparisons - assert exact equality, not a tolerance.

## Constraints

- Layer: `Renderer` only. No `Presentation`, `App`, `IO`, `Domain`, ImGui, GLFW or GL include.
- No exceptions. Failures are `Result<T>` + `StructuredError` (`Core/Diagnostics/StructuredError.hpp`),
  built the same way `PathEvaluator.cpp` builds them. `PathDiagnosticCode` now carries the topology
  codes: `InvalidSegmentIndex`, `ParameterOutOfRange` (finite `t` outside `(0, 1)`), `UnknownElement`
  (no node with that id), `MergeRequiresLineNeighbours` (interior delete with a non-Line neighbour),
  `LastNodeNotRemovable`, `TangentNotApplicable` (a legal rigid corner). Degenerate geometry keeps
  `ZeroChord` / `NonFinite`. If something still has no fitting code, STOP and report it rather than
  extending the enum yourself.
- Storage `float`, evaluation `double` (plan v2 C9). Reuse `PathEvaluator`'s `DeriveArc` and
  `EvaluateSegment` - do NOT write a second arc derivation or a second cubic evaluator.
- `.cpp` files stay under ~500 lines.
- Tabs, `DefectStudio` namespace, brace style as in `src/Renderer/Path/PathEvaluator.cpp`.
- Every `.cpp` starts with `#include "Core/dspch.hpp"`.
- Tests in `namespace DefectStudio::Tests`, style as `tests/Renderer/Path/PathEvaluatorTests.cpp`.
  Note: glm has explicit constructors here - write `glm::dvec3(0.0)`, not `{0.0}`.
- Do not build, do not run `scripts/Windows/GenerateProjects.bat`, do not wait for approval - the
  dispatching session builds and verifies.
