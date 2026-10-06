from __future__ import annotations

import json
import os
import pathlib
import sys
import tempfile

try:
    from puntukas.vasp import VaspOutput
except ImportError as exc:
    print(json.dumps({"error": "puntukas_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)


def load_orbital_grid_payload(directory: str, ispin: int, ikpt: int, band: int) -> dict:
    resolved = pathlib.Path(directory).resolve()
    output = VaspOutput.from_directory(str(resolved))
    if output.wavecar is None:
        raise FileNotFoundError("Missing wavecar")

    # wavefunction() returns a PlaneWaveField; to_real_space() FFTs it onto the reader's own
    # minimal fast grid. The real part keeps the wavefunction's sign, which is needed for +/- lobe
    # isosurface coloring, unlike |psi|^2 which discards it.
    # `band` here is VASP's own 1-based number (matching OUTCAR/EIGENVAL and the orbital table in
    # vasp_output_load.py's _orbitals_payload) - Wavecar.wavefunction indexes the WAVECAR band array
    # 0-based, hence the -1. Keep this in sync with that other conversion: this is the grid-fetch
    # counterpart to the same band the table already displays.
    field = output.wavecar.wavefunction(ispin, ikpt, max(band - 1, 0))
    real_space = field.to_real_space()
    grid = real_space.data.real.astype("float32")

    # Grid is too large for a JSON-line payload (up to tens of MB) - write it as a raw contiguous
    # float32 file (C-order, x slowest/z fastest) and hand back the path; the caller reads it and
    # deletes it.
    handle, grid_path = tempfile.mkstemp(suffix=".bin", prefix="ds_orbital_grid_")
    os.close(handle)
    grid.tofile(grid_path)

    return {
        "gridPath": grid_path,
        "dims": list(grid.shape),
        "cell": real_space.cell.array.tolist(),
        "energy": float(field.meta["energy"]),
        "occupation": float(field.meta["occupation"]),
    }


def main() -> int:
    if len(sys.argv) < 5:
        raise SystemExit(
            "usage: vasp_orbital_grid_load.py <calculation_directory> <ispin> <ikpt> <band>")

    directory = sys.argv[1]
    ispin = int(sys.argv[2])
    ikpt = int(sys.argv[3])
    band = int(sys.argv[4])

    payload = load_orbital_grid_payload(directory, ispin, ikpt, band)
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
