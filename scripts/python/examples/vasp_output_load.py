from __future__ import annotations

import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

try:
    from puntukas.vasp import VaspOutput

    from common.puntukas_compat import patch_incar_tolerant_encoding
    patch_incar_tolerant_encoding()
except ImportError as exc:
    print(json.dumps({"error": "puntukas_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)


def _parse_eigenval_bandgap(directory: str) -> dict | None:
    # Fallback for when vasprun.xml's own eigenvalues/occupations are unavailable (seen on real
    # fixtures - Vasprun.homo/lumo raise TypeError because vasprun.eigenvalues is None even
    # though vasprun.xml itself parses fine). EIGENVAL is VASP's raw per-kpoint/per-band
    # energy+occupation dump - puntukas has no parser for it, so this reads the fixed text layout
    # directly: header lines 1-7, then per kpoint a "kx ky kz weight" line followed by NBANDS
    # lines of "index energy[_up] [energy_down] occ[_up] [occ_down]" (3 columns for ISPIN=1, 5 for
    # ISPIN=2 - splitting each band line in half around the index column handles both without
    # needing to branch on ISPIN). homo/lumo computed globally across all kpoints+spins+bands,
    # matching Vasprun.homo/lumo's own convention.
    path = pathlib.Path(directory) / "EIGENVAL"
    if not path.exists():
        return None

    try:
        lines = path.read_text().splitlines()
        nkpts, nbands = (int(value) for value in lines[5].split()[1:3])

        occ_threshold = 1e-6
        homo = None
        lumo = None

        cursor = 7  # first kpoint line (0-indexed), after the fixed 6-line header + blank line
        for _ in range(nkpts):
            cursor += 1  # kpoint coordinates/weight line
            for _ in range(nbands):
                fields = lines[cursor].split()
                cursor += 1
                half = (len(fields) - 1) // 2
                energies = fields[1:1 + half]
                occupations = fields[1 + half:1 + 2 * half]
                for energy_str, occ_str in zip(energies, occupations):
                    energy = float(energy_str)
                    if float(occ_str) > occ_threshold:
                        homo = energy if homo is None else max(homo, energy)
                    else:
                        lumo = energy if lumo is None else min(lumo, energy)
            cursor += 1  # blank line separating kpoint blocks

        if homo is None or lumo is None:
            return None
        return {"bandgap": lumo - homo, "homo": homo, "lumo": lumo}
    except (OSError, IndexError, ValueError):
        return None


def _band_gap_payload(output, directory: str) -> dict | None:
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
        return _parse_eigenval_bandgap(directory)


def _orbitals_payload(output, band_start: int, band_end: int) -> tuple[list[dict] | None, str | None]:
    # get_orbital_data_for_two_spins raises FileNotFoundError if WAVECAR is absent, and
    # AssertionError if WAVECAR exists but its own internal header is unreadable/inconsistent
    # (observed on a real file: k-point count read as 0, on a network drive - could be a
    # corrupted/incompletely-transferred file, not something to guess about here). Either way,
    # band gap data above can still be useful without orbitals, so this is reported as
    # unavailable, not fatal - but the two cases get different messages (returned as the second
    # tuple element) so "no WAVECAR" and "WAVECAR present but unreadable" aren't indistinguishable
    # in the UI - a user staring at a folder that plainly has a WAVECAR in it needs to know it's
    # the second case, not go looking for a file that's already there.
    # irreps=False (puntukas' own default): symmetry-labeling each band is real per-band cost
    # (get_symmetry over the structure) that this UI doesn't currently display anywhere - a wide
    # band range with irreps=True was observed to be dramatically slower than the same range
    # without it.
    try:
        rows = output.get_orbital_data_for_two_spins(band_start, band_end, irreps=False)
    except FileNotFoundError:
        return None, None
    except AssertionError as exc:
        return None, f"WAVECAR present but unreadable ({exc or 'header assertion failed'}) - " \
            "possibly corrupted or incompletely transferred (seen on network drives)"

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
    return records, None


def load_vasp_output_payload(directory: str, band_start: int, band_end: int) -> dict:
    resolved = pathlib.Path(directory).resolve()
    output = VaspOutput.from_directory(str(resolved))
    orbitals, orbitals_error = _orbitals_payload(output, band_start, band_end)
    return {
        "path": str(resolved),
        "gap": _band_gap_payload(output, str(resolved)),
        "orbitals": orbitals,
        "orbitals_error": orbitals_error,
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
