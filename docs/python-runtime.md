# Python runtime

## Bridge choice

New structure loads use `PuntukasBridge`, backed by the local ASE-`Atoms` package
`punktukas-tools` (`C:\Users\fzabi\punktukas-tools`), rather than `PymatgenBridge`.
The C++ entry points are [`PuntukasBridge.hpp`](../src/ScientificRuntime/Python/PuntukasBridge.hpp)
and [`PymatgenBridge.hpp`](../src/ScientificRuntime/Python/PymatgenBridge.hpp).

Bridge implementations and process execution live under `src/ScientificRuntime/Python/`;
scripts live under [`scripts/python`](../scripts/python). `ScriptRunner::RunFile` invokes
[`Core/Platform/ProcessRunner.hpp`](../src/Core/Platform/ProcessRunner.hpp).

With `DS_PYTHON_CAPI_AVAILABLE=0`, every call is a subprocess call. Each call pays cold import
cost; cancellation and timeout are passed through `ProcessRunOptions`.

`punktukas-tools` provides periodic-MIC distances through
`puntukas.atoms.base.AtomsBase.get_distances`, backed by phonopy `find_mic`. It does not provide
structure comparison or atom matching. `Domain/Crystal/StructureComparison.{hpp,cpp}` and
`ScientificRuntime/Python/ScipyAssignmentBridge` are therefore project code, not wrappers.
