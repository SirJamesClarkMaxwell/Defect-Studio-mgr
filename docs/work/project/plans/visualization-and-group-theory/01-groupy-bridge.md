# 1. `groupy` bridge

Branch: `task/19-groupy-bridge-spike` · Source: locked plan point 8 (bridge half)

## Scope

Get point-group results out of the `groupy` Python package and into C++ as structured data. No UI,
no scene objects, no character-table rendering — those are workstream 8.

- Use the existing Python bridge/job system for analysis; do not implement a second group-theory
  engine in C++.
- Return operation-to-atom/component mappings, displacements, orientations, and phase information.
- Design and test the serialization contract against both symbolic and numeric paths.

## Reuse in this repo

- `src/ScientificRuntime/Python/` — `PuntukasBridge`, `ScipyAssignmentBridge`,
  `VaspOrbitalGridBridge` are the shape to copy. `ScriptRunner::RunFile` is the subprocess entry.
- `src/ScientificRuntime/Python/GetSymmetryInfoJob` — existing spglib-based symmetry detection,
  reuse its tolerance handling rather than adding a second one.
- `groupy` is installed at `C:\Users\fzabi\Desktop\dev\Python\Lib\site-packages\groupy`
  (`pointgroups`, `ireps`, `repr`, `multiplets`, `rotations`, `double_groups`, `hamiltonians`).

## Constraints

- `DS_PYTHON_CAPI_AVAILABLE=0` — every call is a subprocess with a cold import. Measured `import
  groupy` cost: ~1.5 s (SymEngine unavailable, SymPy fallback). Every call MUST go through
  `JobSystem`; never block the UI thread.
- Symbolic results (SymPy objects) do not serialize to JSON on their own. The contract must state
  how exact values cross the boundary (string form plus numeric form is the expected answer, to be
  confirmed by the spike).
