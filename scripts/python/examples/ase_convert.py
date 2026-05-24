from __future__ import annotations

import json
import pathlib
import sys

from ase.io import read, write


def main() -> int:
    if len(sys.argv) < 5:
        raise SystemExit("usage: ase_convert.py <input_path> <output_path> <input_fmt> <output_fmt>")

    input_path = pathlib.Path(sys.argv[1]).resolve()
    output_path = pathlib.Path(sys.argv[2]).resolve()
    input_format = sys.argv[3]
    output_format = sys.argv[4]

    atoms = read(str(input_path), format=input_format)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    write(str(output_path), atoms, format=output_format)

    payload = {
        "output_path": str(output_path),
        "chemical_formula": atoms.get_chemical_formula(),
        "atom_count": len(atoms),
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
