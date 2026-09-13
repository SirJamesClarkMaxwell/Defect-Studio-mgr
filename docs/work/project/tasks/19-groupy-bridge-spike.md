# Task 19: groupy bridge spike

Workstream: [1. groupy bridge](../plans/visualization-and-group-theory/01-groupy-bridge.md)
Branch: `task/19-groupy-bridge-spike`

## Goal

Prove that point-group results can cross the Python↔C++ boundary as structured data. One bridge,
one job, one Python script: given a hardcoded C₃ᵥ NV⁻ basis (N + 3×C dangling bonds), return the
reducible representation's decomposition `A1 ⊕ A1 ⊕ E` plus the projected symmetry-adapted
coefficients, as C++ structs. No UI panel, no scene objects, no character-table rendering.

The spike answers one question: **can `groupy`'s exact (SymPy) results be serialized without losing
their exactness, and what does that payload look like?** Everything downstream in this plan is
designed against the answer.

## Files to create or change

- `src/Domain/Symmetry/PointGroupAnalysis.hpp` (new) — plain result structs, no Python types, no
  renderer/UI includes.
- `src/ScientificRuntime/Python/GroupTheoryBridge.{hpp,cpp}` (new) — subprocess call, JSON in/out.
- `src/ScientificRuntime/Python/ReducePointGroupRepresentationJob.{hpp,cpp}` (new) — `IJob`,
  mirroring `GetSymmetryInfoJob` exactly.
- `scripts/python/examples/groupy_reduce_representation.py` (new) — the worker script.
- `tests/ScientificRuntime/GroupTheoryBridgeTests.cpp` (new).
- `premake5.lua` — only if a new source directory needs registering.
- `requirements` / venv: `groupy_symmetry` must be installed into `.venv` (source repo at
  `C:\Users\fzabi\Desktop\dev\groupy`, currently only installed in a different interpreter). Record
  how it was installed in this file's "Notes" section.

## Files that must NOT be touched

- Anything under `src/Presentation/`, `src/Renderer/`, `src/App/`.
- Any existing bridge (`PuntukasBridge`, `PymatgenBridge`, `SupercellBridge`,
  `ScipyAssignmentBridge`, `VaspOrbitalGrid*`).
- `src/Core/Platform/` — the spike uses `ScriptRunner` as-is.
- Any other file under `docs/work/project/plans/`.

## Acceptance criteria

1. `GroupTheoryBridgeTests` contains a test that builds the NV⁻ four-bond basis in C₃ᵥ, calls the
   bridge, and asserts the decomposition is exactly two `A1` plus one `E` (multiplicity 2).
2. A second test asserts the projected coefficients for `a1'`, `a1`, `ex`, `ey` come back with both
   an exact string form and a numeric form, and that the numeric form matches the string form when
   evaluated.
3. A third test asserts that a bridge failure (bad point-group label) returns a `StructuredError`,
   not an exception and not a crash.
4. `DefectStudioTests.exe` passes in Release with the expected 2 skips and no new failures.
5. `architecture-boundary-review` reports no new violations.

## Constraints

- `DS_PYTHON_CAPI_AVAILABLE=0`. Every call is a subprocess with a cold import. Measured on this
  machine: bare Python 64 ms, `import groupy` 680 ms. The bridge is therefore **one subprocess call
  per request, batched** — follow `ScipyAssignmentBridge`, which solves every matrix in one call for
  exactly this reason. Never one call per operation.
- The bridge is never called from the main thread directly; `ReducePointGroupRepresentationJob` is
  the only supported entry point for UI code (there is no UI code yet — the tests may call the
  bridge directly).
- `groupy` prints a `RuntimeWarning: SymEngine not available` to stderr on import. Keep the JSON
  payload on stdout and treat stderr as diagnostics only, or the parse will break.
- Exact values are SymPy objects and do not JSON-serialize on their own. Carry both `"exact"`
  (string, e.g. `"sqrt(2)/2"`) and `"numeric"` (double). Do not silently drop the exact form.
- `Domain/Symmetry/PointGroupAnalysis.hpp` must compile with no Python, renderer or UI include.
- `.cpp` files stay under ~500 lines.
- Run `scripts/Windows/GenerateProjects.bat` after adding the new sources — premake globs at
  generation time.

## Reference

- Existing shape to copy: `ScipyAssignmentBridge.hpp` (batched subprocess bridge),
  `GetSymmetryInfoJob.hpp` (job wrapper contract), `ScriptBridgeUtils::ResolvePythonExampleScript`
  (script lookup, `scripts/python/examples/`).
- The physics: `C:\Users\fzabi\Desktop\dev\groupy\tutorial\7_NV_center.ipynb`.

## Notes

_(fill in during implementation: how groupy was installed into the venv, what the payload actually
looks like, anything the spike disproved)_
