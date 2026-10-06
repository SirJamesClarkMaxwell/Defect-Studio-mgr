# Python runtime

## Bridge choice

New structure loads use `PuntukasBridge`, backed by the local ASE-`Atoms` package
`puntukas` (`C:\Users\fzabi\puntukas_tools2`), rather than `PymatgenBridge`.
The C++ entry points are [`PuntukasBridge.hpp`](../src/ScientificRuntime/Python/PuntukasBridge.hpp)
and [`PymatgenBridge.hpp`](../src/ScientificRuntime/Python/PymatgenBridge.hpp).

Bridge implementations and process execution live under `src/ScientificRuntime/Python/`;
scripts live under [`scripts/python`](../scripts/python). `ScriptRunner::RunFile` invokes
[`Core/Platform/ProcessRunner.hpp`](../src/Core/Platform/ProcessRunner.hpp).

With `DS_PYTHON_CAPI_AVAILABLE=0`, every call is a subprocess call. Each call pays cold import
cost; cancellation and timeout are passed through `ProcessRunOptions`.

`puntukas.core.Atoms.distances` and `distance_matrix` provide periodic minimum-image distances in
Angstrom. The rewrite provides same-site-order displacement helpers, but not atom assignment across
reordered or composition-changing structures. `Domain/Crystal/StructureComparison.{hpp,cpp}` and
`ScientificRuntime/Python/ScipyAssignmentBridge` therefore remain project code.
