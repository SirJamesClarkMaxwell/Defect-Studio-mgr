# Task 23: group-theory panel

Plan: [scene tools + group-theory panel](../plans/2026-09-13-scene-tools-and-group-theory.md), step 2.
Branch: `task/23-group-theory-panel`

Two parts on one branch:

- **23a — analysis backend (this file's dispatch scope, Codex):** selection basis builder, batched
  `GroupTheoryBridge::Analyze`, worker script, `AnalyzePointGroupJob`.
- **23b — panel UI (Claude, after 23a is verified):** not part of the Codex dispatch.

## Goal

Given the atoms a user selected, one background request returns everything the group-theory panel
shows: detected (or manually chosen) point group with provenance, character table, reducible
characters, decomposition, projected symmetry-adapted vectors and the many-electron multiplets for a
chosen active space. Errors arrive as `StructuredError` with a distinguishing code.

## Files to create or change (23a)

- `src/Domain/Symmetry/PointGroupBasis.cpp` (new) — implements `PointGroupBasis.hpp`.
- `src/ScientificRuntime/Python/GroupTheoryBridge.cpp` — add `Analyze`; keep `ReduceRepresentation`
  working unchanged.
- `src/ScientificRuntime/Python/AnalyzePointGroupJob.cpp` (new) — implements the header.
- `scripts/python/examples/groupy_point_group_analysis.py` (new) — the worker script.
- `premake5.lua` — only if a new source directory needs registering.

Contract, already written — **do not change**: `src/Domain/Symmetry/PointGroupAnalysis.hpp`,
`src/Domain/Symmetry/PointGroupBasis.hpp`, `src/ScientificRuntime/Python/GroupTheoryBridge.hpp`,
`src/ScientificRuntime/Python/AnalyzePointGroupJob.hpp`,
`tests/Domain/Symmetry/PointGroupBasisTests.cpp`,
`tests/ScientificRuntime/PointGroupAnalysisBridgeTests.cpp`.

## Files that must NOT be touched

- Anything under `src/Presentation/`, `src/Renderer/`, `src/App/`.
- `src/Core/` (JobSystem, ScriptRunner are used as-is).
- `scripts/python/examples/groupy_reduce_representation.py` and `GroupTheoryBridgeTests.cpp`
  (task 19 contract stays green).
- Other bridges, `docs/work/project/plans/`.
- `install/app/python/` — never hand-edit the bundled runtime; see Setup.

## Acceptance criteria

1. `PointGroupBasisTests` (6) pass.
2. `PointGroupAnalysisBridgeTests` (11) pass — none skipped.
3. `GroupTheoryBridgeTests` (task 19) still pass.
4. `DefectStudioTests.exe` Release: everything passes except the 2 known skips.
5. `architecture-boundary-review`: no new violations.

## Constraints and design notes

- `DS_PYTHON_CAPI_AVAILABLE=0`: one subprocess per `Analyze` call, cold `import groupy` ~700 ms.
  Never split into several calls. JSON on stdout, stderr diagnostics only (groupy warns there).
  Follow `ReduceRepresentation`'s temp-payload + `ScriptRunner::RunFile` + `ExtractJsonLineFromOutput`
  shape; error JSON on stderr as `{"error": "<suffix>", "detail": ...}` mapped to the codes in
  `GroupTheoryBridge.hpp`.
- **Detection:** pymatgen `PointGroupAnalyzer(Molecule(elements, positions), tolerance=symmetryTolerance)`.
  Map `sch_symbol` to the groupy label (`"C3v"`, `"Cs"`, `"C1"`, ...; `"C*v"`/`"D*h"` linear groups
  -> Undetermined with a reason). Undetermined is a success result, not an error.
- **Frame alignment** (manual and detected): groupy's element matrices assume its standard frame
  (principal axis z, plus its own convention for σv / C2' / σd). Find a proper rotation `R` so that
  `R * positions` closes under groupy's elements within `symmetryTolerance`, element-aware. Suggested
  route: candidate principal axes and secondary directions (σ normals, C2 axes) from the analyzer's
  symmetry operations at the same tolerance; try each assignment onto groupy's convention; accept the
  first `R` that closes. None -> `frame_alignment_failed`. Report `R` as `frameRotation`
  (row-major JSON 3x3, `p_groupy = R * p`).
- **Permutation matching** in groupy's frame: nearest same-element site within
  `symmetryTolerance`, must be unique -> else `basis_not_closed`.
- **Character table:** `pg.class_names`, `pg.ireps.class_sizes`, `pg.ireps[name].values` per irrep;
  each character as `{exact, numeric, numericImaginary}`. Reducible characters = trace of the
  permutation matrix of one element per class.
- **Reduction / projected vectors:** reuse the task 19 algorithm (`reduce_repr`,
  `generalized_projection_operator`, GramSchmidt) — factor it into a shared function inside the new
  script rather than importing the old script.
- **Multiplets:** `ActiveSpace.from_orbitals(pg, activeOrbitalIrreps, nel=activeElectronCount).term_table()`;
  `entries` tuples are `(irrep, S, dΓ, countPerRow, total)`; send `spinMultiplicity = int(2*S + 1)`.
  Unknown irrep / invalid electron count -> `invalid_active_space`. Skip entirely when the irrep list
  is empty or the electron count is 0.
- **Basis builder (C++):** labels `species + index`; periodic: invertibility check on
  `cell.ToMatrix()` (|det| < 1e-8 -> `singular_lattice`), then fractional delta to centre wrapped to
  [-0.5, 0.5) -> Cartesian; hash over index, element, positions quantized to 1e-6 Å (FNV-1a or
  `std::hash` combine, no new dependency).
- **Job:** mirror `ReducePointGroupRepresentationJob`; on failure store the `StructuredError` in
  `m_Error` before throwing `std::runtime_error(userMessage)`.
- `.cpp` files stay under ~500 lines. Regenerate projects after adding sources.

## Setup prerequisite (groupy runtime)

`groupy` is a local package (like `punktukas-tools`), not in `pyproject.toml`. `ScriptRunner` runs
the bundled runtime at `install/app/python/windows/python.exe`, not `.venv`:

```
uv pip install C:/Users/fzabi/Desktop/dev/groupy symengine    # into .venv
python scripts/python/prepare_app_python_runtime.py           # copy .venv site-packages into the runtime
```

`PointGroupAnalysisBridgeTests.GroupyRuntimeIsInstalled` fails (not skips) with this hint when groupy
is missing.

## 23b — panel (after 23a)

Presentation panel: "Use selection as basis" (centre = selection centroid / 3D cursor / atom),
group combo (Detect + manual list) with tolerance, active irreps + electron count, Compute.
Revision token + structure revision + basis hash; commit on the main thread only if they still
match, else keep "stale" + Recompute. Tables: character table, `Γ = 2A₁ ⊕ E` with reducible
characters, projected vectors (exact shown, numeric tooltip), multiplets; copy as Markdown / LaTeX,
Unicode sub/superscripts. Errors shown by code category. Physical names only as marked assumptions.
