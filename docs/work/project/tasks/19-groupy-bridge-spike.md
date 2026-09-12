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

## Contract (written before dispatch - do not change)

`PointGroupAnalysis.hpp`, `GroupTheoryBridge.hpp`, `ReducePointGroupRepresentationJob.hpp` and
`GroupTheoryBridgeTests.cpp` are already written and are the contract. Only the two `.cpp` files and
the Python script are open. Verified: everything compiles, and the only unresolved external is
`GroupTheoryBridge::ReduceRepresentation`.

Three design decisions were made while writing it that deviate from the text above:

1. **Positions, not matrices.** The request carries the point-group label plus named basis sites with
   Cartesian positions; the Python side builds the permutation representation itself, exactly as
   tutorial cell 3 does. Sending element matrices instead would force both languages to agree on the
   order of `pg.elements`. Ceiling, recorded in the header: this covers permutation bases only - an
   orbital basis (p, d, ...) transforms by rotation and will need explicit matrices, which is a field
   to add in workstream 5/6, not a thing to fake with positions now.
2. **Neutral labels.** The bridge returns `irrepLabel` + `occurrenceIndex` + `irrepRow`, not `a1'` /
   `a1` / `ex` / `ey`. Which A1 copy is "lower" is an energy ordering that group theory alone cannot
   determine - it takes the Hamiltonian - so naming them at this layer would be a guess dressed as a
   result. Acceptance criterion 2 is therefore checked as "two A1 occurrences plus both E rows, each
   with an exact and a numeric form", and physical naming moves to the UI in workstream 8, marked as
   an assumption.
3. **The basis must be closed under the group.** The tutorial's `argmin` accepts any nearest site,
   however far. The request carries `matchTolerance` and the script must reject a basis whose rotated
   sites do not land on real sites. Two extra tests cover this and the empty-basis case.

Because C++ cannot evaluate a SymPy string, "numeric matches exact" is checked structurally instead:
zero iff zero, unit norm, and mutual orthogonality of the projected vectors.

## Notes

### How groupy reaches the app (the thing this spike actually cost time on)

`ScriptRunner` does **not** run the project `.venv`. It runs the bundled runtime at
`install/app/python/windows/python.exe`, which has its own `Lib/site-packages`. Installing a package
into `.venv` therefore does nothing for any bridge. The runtime is not pip-installable either - it is
uv-managed and rejects `pip install` and `uv pip install --python ...` with
`externally-managed-environment`.

The supported path is `scripts/python/prepare_app_python_runtime.py`, which copies `.venv`'s
site-packages into the runtime tree. So: install into `.venv` first (here:
`uv pip install C:/Users/fzabi/Desktop/dev/groupy`, plus `symengine`), then re-run the prepare
script. The first run failed with `WinError 32` on a handful of files; a second run went through.

This mattered beyond convenience. With `groupy` missing from the runtime, the two positive tests
skipped and the three negative tests **passed for the wrong reason** - the script was dying on
`import groupy` before it ever reached the unknown-group, non-closed-basis or empty-basis guards. A
green run that proves nothing is worse than a red one. Any future bridge test needs the same check:
confirm the negative case fails where you think it fails.

### The payload

NV(-) four-bond basis in C3v, sites `d` (N, on the axis) and `a`/`b`/`c` (carbons):

```json
{"pointGroupLabel":"C3v","siteLabels":["d","a","b","c"],"groupOrder":6,
 "decomposition":[{"irrepLabel":"A1","multiplicity":2,"dimension":1},
                  {"irrepLabel":"E","multiplicity":1,"dimension":2}],
 "projectedVectors":[
  {"irrepLabel":"A1","occurrenceIndex":0,"irrepRow":0,"coefficients":[
    {"exact":"1","numeric":1.0},{"exact":"0","numeric":0.0},{"exact":"0","numeric":0.0},{"exact":"0","numeric":0.0}]},
  {"irrepLabel":"A1","occurrenceIndex":1,"irrepRow":0,"coefficients":[
    {"exact":"0","numeric":0.0},{"exact":"sqrt(3)/3","numeric":0.5773502691896257},
    {"exact":"sqrt(3)/3","numeric":0.5773502691896257},{"exact":"sqrt(3)/3","numeric":0.5773502691896257}]},
  {"irrepLabel":"E","occurrenceIndex":0,"irrepRow":0,"coefficients":[
    {"exact":"0","numeric":0.0},{"exact":"sqrt(6)/3","numeric":0.816496580927726},
    {"exact":"-sqrt(6)/6","numeric":-0.408248290463863},{"exact":"-sqrt(6)/6","numeric":-0.408248290463863}]},
  {"irrepLabel":"E","occurrenceIndex":0,"irrepRow":1,"coefficients":[
    {"exact":"0","numeric":0.0},{"exact":"0","numeric":0.0},
    {"exact":"sqrt(2)/2","numeric":0.7071067811865476},{"exact":"-sqrt(2)/2","numeric":-0.7071067811865476}]}]}
```

The spike's question is answered: **yes**, exact SymPy results cross the boundary intact as strings
paired with doubles. `sqrt(3)/3`, `sqrt(6)/3`, `sqrt(2)/2` survive; nothing is rounded on the way out.
The N contributes one A1 alone, the three carbons the second A1 and the E pair - which is the
physically expected split, so the transport is not just well-formed but right.

### What it costs

One subprocess per request, ~700 ms wall clock (measured 682-1142 ms across the five tests) dominated
by the cold `import groupy`. That is the real argument for keeping the bridge batched: a UI that
reduces ten representations must send one request with ten, not ten requests.

### Not disproved, but newly visible

`occurrenceIndex` restarts per irrep row, not per irrep. For a 1-D irrep that reads naturally (A1
occurrences 0 and 1). For E it means both rows report `occurrenceIndex: 0` - correct, since there is
one E copy with two rows, but a consumer that groups by `occurrenceIndex` alone across rows will
merge things it should not. Group by the `(irrepLabel, occurrenceIndex, irrepRow)` triple.
