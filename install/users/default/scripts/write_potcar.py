#!/usr/bin/env python3
"""Write POTCAR file by concatenating per-element POTCAR files from pseudopotential directory."""

import sys
import json
import tempfile
import os
import shutil
from pathlib import Path


def write_potcar(
    output_path: str,
    pseudopotential_dir: str,
    species: list[str],
) -> None:
    """Concatenate POTCAR files for each species into single POTCAR.

    Args:
        output_path: Target POTCAR file path
        pseudopotential_dir: Directory containing per-element POTCAR files
        species: List of unique element symbols (e.g., ["Al", "O"])
    """
    pseudo_dir = Path(pseudopotential_dir)
    if not pseudo_dir.is_dir():
        raise ValueError(f"Pseudopotential directory not found: {pseudopotential_dir}")

    potcar_content = []
    for element in species:
        # Expect POTCAR file named "POTCAR" in element subdirectory
        # (standard VASP pseudopotential structure)
        potcar_path = pseudo_dir / element / "POTCAR"
        if not potcar_path.exists():
            # Fallback: try uppercase element name
            potcar_path = pseudo_dir / element.upper() / "POTCAR"
        if not potcar_path.exists():
            raise ValueError(f"POTCAR not found for element {element} in {pseudopotential_dir}")

        with open(potcar_path, "r") as f:
            potcar_content.append(f.read())

    # Write to temp file first, then atomic rename
    tmp_fd, tmp_path = tempfile.mkstemp(suffix=".tmp", dir=str(Path(output_path).parent))
    try:
        os.close(tmp_fd)
        with open(tmp_path, "w") as f:
            f.write("".join(potcar_content))
        shutil.move(tmp_path, output_path)
    except Exception:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)
        raise


def main() -> None:
    """CLI entry point: read JSON from first arg (file path), write POTCAR."""
    try:
        if len(sys.argv) < 2:
            raise ValueError("Usage: write_potcar.py <json_input_file>")

        json_file = sys.argv[1]
        with open(json_file, "r") as f:
            input_data = json.load(f)

        output_path = input_data.get("output_path")
        if not output_path:
            raise ValueError("Missing 'output_path' in input JSON")

        pseudopotential_dir = input_data.get("pseudopotential_dir")
        if not pseudopotential_dir:
            raise ValueError("Missing 'pseudopotential_dir' in input JSON")

        species = input_data.get("species", [])
        if not species:
            raise ValueError("Missing or empty 'species' in input JSON")

        write_potcar(output_path, pseudopotential_dir, species)
        print(json.dumps({"success": True, "output_path": output_path}))
    except Exception as e:
        sys.stderr.write(f"ERROR: {e}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
