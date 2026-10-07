"""One CHGCAR density component as a raw float32 grid for the C++ side (VaspDensityGridBridge).

usage: vasp_density_grid_load.py <chgcar> <component> [<reference_chgcar>]

component: total | magnetization | up | down. With a reference the reference's same component is
subtracted on the same grid (rho(q) - rho(0) and friends). Values are physical densities, e/Ang^3
(puntukas divides the raw rho*Omega by the cell volume). Same output contract as
vasp_orbital_grid_load.py: one JSON line, the grid in a temp file (C-order, x slowest), which the
caller reads and deletes.
"""
from __future__ import annotations

import json
import os
import sys
import tempfile

try:
    import numpy as np
    from puntukas.vasp import Chgcar
except ImportError as exc:
    print(json.dumps({"error": "puntukas_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)

COMPONENTS = ("total", "magnetization", "up", "down")


def component_field(chgcar: Chgcar, component: str):
    if component == "total":
        return chgcar.charge_density()
    if component == "magnetization":
        field = chgcar.magnetization_density()
        if isinstance(field, tuple):
            raise ValueError("noncollinear CHGCAR: magnetization is a vector field, pick total")
        return field
    return chgcar.spin_density(component)


def load(path: str, component: str, reference: str | None) -> dict:
    if component not in COMPONENTS:
        raise ValueError(f"component must be one of {COMPONENTS}, got {component!r}")
    field = component_field(Chgcar.from_file(path), component)
    data = field.data
    if reference:
        ref = component_field(Chgcar.from_file(reference), component)
        if ref.shape != field.shape:
            raise ValueError(f"grid mismatch: {field.shape} vs reference {ref.shape}")
        if not np.allclose(ref.cell.array, field.cell.array, atol=1e-4):
            raise ValueError("cell mismatch between the CHGCAR and its reference")
        data = data - ref.data

    grid = np.ascontiguousarray(data, dtype=np.float32)
    handle, grid_path = tempfile.mkstemp(suffix=".bin", prefix="ds_density_grid_")
    os.close(handle)
    grid.tofile(grid_path)

    dv = field.dV
    return {
        "gridPath": grid_path,
        "dims": list(grid.shape),
        "cell": field.cell.array.tolist(),
        "integral": float(data.sum() * dv),
        "absIntegral": float(np.abs(data).sum() * dv),
        "min": float(data.min()),
        "max": float(data.max()),
        "atomCount": len(field.atoms) if field.atoms is not None else 0,
    }


def main() -> int:
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__.splitlines()[2])
    payload = load(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) == 4 else None)
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
