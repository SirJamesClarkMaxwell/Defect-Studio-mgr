#!/usr/bin/env python3
"""Write POSCAR structure file via ASE (ase.io.write)."""

import sys
import json
import tempfile
import os
from pathlib import Path

try:
    from ase.io import write
    from ase import Atoms
except ImportError as e:
    sys.stderr.write(f"ERROR: ASE not available: {e}\n")
    sys.exit(1)


def write_poscar(
    output_path: str,
    species: list[str],
    positions: list[list[float]],
    cell: list[list[float]],
    pbc: list[bool] = None,
    overwrite: bool = False,
) -> None:
    """Write POSCAR file from structure data.

    Args:
        output_path: Target POSCAR file path
        species: List of element symbols, one per atom
        positions: Atomic positions [Natom x 3] (fractional coords)
        cell: Unit cell vectors [3 x 3] (Angstrom)
        pbc: Periodic boundary conditions [3] (default: [True, True, True])
    """
    if pbc is None:
        pbc = [True, True, True]

    if len(species) != len(positions):
        raise ValueError(f"species count {len(species)} != positions count {len(positions)}")

    # positions arrive as FRACTIONAL coords, so scaled_positions - `positions=` would
    # treat them as Angstrom cartesians and silently write a wrong structure.
    atoms = Atoms(symbols=species, scaled_positions=positions, cell=cell, pbc=pbc)

    # Write to temp file first, then atomically commit it. The commit must FAIL if the destination
    # already exists rather than replacing it - shutil.move/os.replace would silently overwrite a
    # POSCAR that appeared between the caller's collision check and this write.
    tmp_fd, tmp_path = tempfile.mkstemp(suffix=".tmp", dir=str(Path(output_path).parent))
    try:
        os.close(tmp_fd)
        write(tmp_path, atoms, format='vasp')
        if overwrite:
            os.replace(tmp_path, output_path)
        elif os.name == "nt":
            # Windows os.rename already refuses an existing destination.
            os.rename(tmp_path, output_path)
        else:
            # POSIX os.rename silently replaces; link+unlink is the no-replace equivalent.
            os.link(tmp_path, output_path)
            os.unlink(tmp_path)
    except Exception:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)
        raise


def main() -> None:
    """CLI entry point: read JSON from first arg (file path), write POSCAR."""
    try:
        if len(sys.argv) < 2:
            raise ValueError("Usage: write_poscar.py <json_input_file>")

        json_file = sys.argv[1]
        with open(json_file, "r") as f:
            input_data = json.load(f)

        output_path = input_data.get("output_path")
        if not output_path:
            raise ValueError("Missing 'output_path' in input JSON")

        species = input_data.get("species", [])
        positions = input_data.get("positions", [])
        cell = input_data.get("cell", [])
        pbc = input_data.get("pbc", [True, True, True])
        overwrite = input_data.get("overwrite", False)

        write_poscar(output_path, species, positions, cell, pbc, overwrite)
        print(json.dumps({"success": True, "output_path": output_path}))
    except Exception as e:
        sys.stderr.write(f"ERROR: {e}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
