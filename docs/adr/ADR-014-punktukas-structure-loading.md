# ADR-014 – Puntukas as the Default Structure-Loading Bridge

Status: Accepted

## Decision

Use `PuntukasBridge` for new structure loads. Keep `PymatgenBridge` for existing operations that
explicitly require its contract.

## Evidence

`OpenDefectJob` and `CompareStructuresJob` use `PuntukasBridge`; both bridge implementations remain
under `src/ScientificRuntime/Python`.

## Consequences

New loading flows should extend the Puntukas path unless a documented capability requires pymatgen.
