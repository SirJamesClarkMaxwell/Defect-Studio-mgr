from __future__ import annotations

import json
import pathlib
import sys

try:
    from puntukas.core import Atoms
except ImportError as exc:
    print(json.dumps({"error": "puntukas_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)

def load_structure_payload(raw_path: str) -> dict:
    input_path = pathlib.Path(raw_path).resolve()
    atoms = Atoms.from_file(input_path)

    symbols = [str(symbol) for symbol in atoms.symbols]
    fractional = atoms.scaled_positions.tolist()
    cartesian = atoms.positions.tolist()
    charges = atoms.ase.get_initial_charges().tolist() if atoms.ase.has("initial_charges") else None
    magmoms = atoms.magnetic_moments.tolist() if atoms.ase.has("initial_magmoms") else None

    return {
        "path": str(input_path),
        "reduced_formula": atoms.ase.get_chemical_formula(mode="reduce"),
        "lattice": atoms.cell.array.tolist(),
        "sites": [
            {
                "element": symbols[i],
                "fractional": fractional[i],
                "cartesian": cartesian[i],
                # The shared bridge schema permits this field, but this loader intentionally
                # preserves its existing contract and does not translate ase constraints yet.
                "selective_dynamics": None,
                "charge": charges[i] if charges is not None else None,
                "magmom": magmoms[i] if magmoms is not None else None,
                "occupancy": 1.0,
            }
            for i in range(len(symbols))
        ],
    }


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: puntukas_structure_load.py <structure_path> [structure_path ...]")

    payloads = [load_structure_payload(raw_path) for raw_path in sys.argv[1:]]
    payload = payloads[0] if len(payloads) == 1 else {"structures": payloads}
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
