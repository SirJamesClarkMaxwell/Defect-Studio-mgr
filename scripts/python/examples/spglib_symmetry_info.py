from __future__ import annotations

import json
import sys

import spglib


def main() -> int:
    payload_path, symprec = sys.argv[1], float(sys.argv[2])
    with open(payload_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    lattice = payload["lattice"]
    positions = [site["fractional"] for site in payload["sites"]]
    elements = [site["element"] for site in payload["sites"]]
    unique = sorted(set(elements))
    numbers = [unique.index(e) for e in elements]

    dataset = spglib.get_symmetry_dataset((lattice, positions, numbers), symprec=symprec)
    print(json.dumps({
        "spacegroup_number": dataset.number,
        "spacegroup_symbol": dataset.international,
        "point_group_symbol": dataset.pointgroup,
        "wyckoffs": list(dataset.wyckoffs),
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
