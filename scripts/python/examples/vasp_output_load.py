from __future__ import annotations

import json
import pathlib
import sys

try:
    from puntukas.vasp import VaspOutput
except ImportError as exc:
    print(json.dumps({"error": "puntukas_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)


def _band_gap_payload(output) -> dict | None:
    # VaspOutput.bandgap delegates to vasprun.bandgap with no None-guard - raises AttributeError
    # if vasprun.xml is missing. homo/lumo are global across all spins+kpoints (see
    # puntukas/vasp/vasprun/vasprun.py Vasprun.homo/lumo), not spin-resolved.
    try:
        return {
            "bandgap": float(output.bandgap),
            "homo": float(output.vasprun.homo),
            "lumo": float(output.vasprun.lumo),
        }
    except (AttributeError, TypeError):
        return None


def _orbitals_payload(output, band_start: int, band_end: int) -> list[dict] | None:
    # get_orbital_data_for_two_spins raises FileNotFoundError if WAVECAR is absent - band gap
    # data above can still be useful without it, so this is reported as unavailable, not fatal.
    try:
        rows = output.get_orbital_data_for_two_spins(band_start, band_end, irreps=True)
    except FileNotFoundError:
        return None

    has_irrep = "irrep(up)" in rows.dtype.names
    records = []
    for row in rows:
        records.append({
            "band": int(row["nr"]),
            "up": {
                "energy": float(row["e(up)"]),
                "occupation": float(row["occ(up)"]),
                "localization": float(row["loc(up)"]),
                "irrep": str(row["irrep(up)"]) if has_irrep else None,
            },
            "down": {
                "energy": float(row["e(down)"]),
                "occupation": float(row["occ(down)"]),
                "localization": float(row["loc(down)"]),
                "irrep": str(row["irrep(down)"]) if has_irrep else None,
            },
        })
    return records


def load_vasp_output_payload(directory: str, band_start: int, band_end: int) -> dict:
    resolved = pathlib.Path(directory).resolve()
    output = VaspOutput.from_directory(str(resolved))
    return {
        "path": str(resolved),
        "gap": _band_gap_payload(output),
        "orbitals": _orbitals_payload(output, band_start, band_end),
    }


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: vasp_output_load.py <calculation_directory> [band_start] [band_end]")

    directory = sys.argv[1]
    band_start = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    band_end = int(sys.argv[3]) if len(sys.argv) > 3 else 10

    payload = load_vasp_output_payload(directory, band_start, band_end)
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
