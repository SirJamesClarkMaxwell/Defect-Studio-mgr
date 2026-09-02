from __future__ import annotations

import json
import sys

from ase import Atoms
from ase.db import connect


def cmd_add(db_path: str, payload_path: str) -> dict:
    with open(payload_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    atoms = Atoms(
        symbols=[site["element"] for site in payload["sites"]],
        scaled_positions=[site["fractional"] for site in payload["sites"]],
        cell=payload["lattice"],
        pbc=True,
    )
    db = connect(db_path)
    row_id = db.write(atoms, name=payload["name"], notes=payload.get("notes", ""))
    return {"id": str(row_id), "name": payload["name"], "reduced_formula": atoms.get_chemical_formula(), "notes": payload.get("notes", "")}


def cmd_list(db_path: str) -> dict:
    db = connect(db_path)
    entries = [
        {
            "id": str(row.id),
            "name": row.key_value_pairs.get("name", ""),
            "reduced_formula": row.formula,
            "notes": row.key_value_pairs.get("notes", ""),
        }
        for row in db.select()
    ]
    return {"entries": entries}


def cmd_load(db_path: str, entry_id: str) -> dict:
    db = connect(db_path)
    atoms = db.get(id=int(entry_id)).toatoms()
    return {
        "lattice": atoms.cell.tolist(),
        "sites": [
            {"element": symbol, "fractional": frac.tolist()}
            for symbol, frac in zip(atoms.get_chemical_symbols(), atoms.get_scaled_positions())
        ],
    }


def cmd_remove(db_path: str, entry_id: str) -> dict:
    db = connect(db_path)
    db.delete([int(entry_id)])
    return {"removed_id": entry_id}


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit("usage: ase_material_library.py <db_path> <add|list|load|remove> [arg]")

    db_path, operation = sys.argv[1], sys.argv[2]
    handlers = {"add": lambda: cmd_add(db_path, sys.argv[3]), "list": lambda: cmd_list(db_path),
                "load": lambda: cmd_load(db_path, sys.argv[3]), "remove": lambda: cmd_remove(db_path, sys.argv[3])}
    if operation not in handlers:
        raise SystemExit(f"unknown operation: {operation}")

    print(json.dumps(handlers[operation]()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
