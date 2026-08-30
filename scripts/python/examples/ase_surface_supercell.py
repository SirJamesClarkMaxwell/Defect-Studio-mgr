from __future__ import annotations

import json
import sys

import numpy as np
from ase import Atoms
from ase.build import surface


def main() -> int:
    payload_path, h, k, l, layers = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
    with open(payload_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    bulk = Atoms(
        symbols=[site["element"] for site in payload["sites"]],
        scaled_positions=[site["fractional"] for site in payload["sites"]],
        cell=payload["lattice"],
        pbc=True,
    )
    slab = surface(bulk, (h, k, l), layers, periodic=True)

    # slab.cell = transform @ bulk.cell (row-vector convention) - recover the integer transform by
    # solving the linear system; surface() builds the slab from integer lattice combinations, so
    # the exact solution is integral within float noise, hence the round().
    transform = np.round(
        np.linalg.solve(np.array(payload["lattice"]).T, np.array(slab.cell).T).T
    ).astype(int)

    print(json.dumps({"matrix": transform.tolist()}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
