from __future__ import annotations

import json
import pathlib
import sys

from pymatgen.core import Structure


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit("usage: pymatgen_poscar_roundtrip.py <input_poscar> <output_poscar>")

    input_path = pathlib.Path(sys.argv[1]).resolve()
    output_path = pathlib.Path(sys.argv[2]).resolve()

    structure = Structure.from_file(input_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    structure.to(filename=str(output_path), fmt="poscar")

    payload = {
        "output_path": str(output_path),
        "reduced_formula": structure.composition.reduced_formula,
        "site_count": len(structure.sites),
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
