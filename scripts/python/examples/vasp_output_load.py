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


# Hermann-Mauguin (spglib's `dataset["pointgroup"]`) -> Schoenflies, the 32 crystallographic point
# groups. spglib/puntukas expose no Schoenflies symbol directly - this is a static, well-known
# bijection, not something worth a dependency for.
_POINT_GROUP_HM_TO_SCHOENFLIES = {
    "1": "C1", "-1": "Ci", "2": "C2", "m": "Cs", "2/m": "C2h", "222": "D2", "mm2": "C2v",
    "mmm": "D2h", "4": "C4", "-4": "S4", "4/m": "C4h", "422": "D4", "4mm": "C4v", "-42m": "D2d",
    "4/mmm": "D4h", "3": "C3", "-3": "C3i", "32": "D3", "3m": "C3v", "-3m": "D3d", "6": "C6",
    "-6": "C3h", "6/m": "C6h", "622": "D6", "6mm": "C6v", "-6m2": "D3h", "6/mmm": "D6h", "23": "T",
    "m-3": "Th", "432": "O", "-43m": "Td", "m-3m": "Oh",
}


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


def _summary_payload(output) -> dict:
    # Each field independently try/excepted - a partial/older OUTCAR or vasprun.xml can have some
    # of these and not others (e.g. no WAVECAR needed here at all, unlike orbitals below), and one
    # missing field shouldn't blank out the rest of the summary.
    summary: dict = {}

    try:
        # Vasprun._etot is private (no public per-step API - Vasprun.etot only returns the very
        # last value) but is exactly the "how did it converge" trend the summary panel wants: one
        # entry per ionic step, itself the array of that step's SCF iterations - last SCF value of
        # each step is the step's converged energy.
        summary["energy_trend"] = [float(step[-1]) for step in output.vasprun._etot]
    except (AttributeError, TypeError, IndexError):
        summary["energy_trend"] = None

    try:
        summary["final_energy"] = float(output.etot)
    except (AttributeError, TypeError):
        summary["final_energy"] = None

    for field in ("cpu_time", "user_time", "system_time", "elapsed_time"):
        try:
            value = getattr(output.outcar, field)
            summary[field] = float(value) if value is not None else None
        except (AttributeError, TypeError):
            summary[field] = None

    try:
        drift = output.drift
        summary["total_drift"] = [float(component) for component in drift] if drift is not None else None
    except (AttributeError, TypeError):
        summary["total_drift"] = None

    try:
        summary["nelect"] = float(output.vasprun.NELECT)
    except (AttributeError, TypeError):
        summary["nelect"] = None
    try:
        summary["ispin"] = int(output.vasprun.ISPIN)
    except (AttributeError, TypeError):
        summary["ispin"] = None

    try:
        summary["pressure"] = float(output.vasprun.get_pressure())
    except (AttributeError, TypeError):
        summary["pressure"] = None
    try:
        stress = output.vasprun.get_stress_tensor()
        summary["stress_tensor"] = [[float(v) for v in row] for row in stress] if stress is not None else None
    except (AttributeError, TypeError):
        summary["stress_tensor"] = None

    try:
        from puntukas import Symmetry
        sym = Symmetry(output.atoms)
        summary["space_group_symbol"] = str(sym.international_symbol)
        summary["space_group_number"] = int(sym.spacegroup_number)
    except (ImportError, AttributeError, TypeError):
        summary["space_group_symbol"] = None
        summary["space_group_number"] = None

    try:
        # sym.dataset is spglib's raw dataset dict - no public Symmetry property wraps
        # "pointgroup" the way international_symbol/spacegroup_number wrap "international"/
        # "number", so read it directly (same class of private-ish access already used for
        # Vasprun._etot above).
        point_group_hm = str(sym.dataset["pointgroup"])
        summary["point_group_symbol"] = point_group_hm
        summary["point_group_schoenflies"] = _POINT_GROUP_HM_TO_SCHOENFLIES.get(point_group_hm)
    except (ImportError, AttributeError, TypeError, KeyError, NameError):
        summary["point_group_symbol"] = None
        summary["point_group_schoenflies"] = None

    return summary


def _orbitals_payload(
    output, band_start: int, band_end: int, irreps: bool = False, irrep_tol: float = 1e-1,
    symprec: float = 1e-3) -> tuple[list[dict] | None, str | None]:
    # get_orbital_data_for_two_spins raises FileNotFoundError if WAVECAR is absent, and
    # AssertionError if WAVECAR exists but its own internal header is unreadable/inconsistent
    # (observed on a real file: k-point count read as 0, on a network drive - could be a
    # corrupted/incompletely-transferred file, not something to guess about here). Either way,
    # band gap data above can still be useful without orbitals, so this is reported as
    # unavailable, not fatal - but the two cases get different messages (returned as the second
    # tuple element) so "no WAVECAR" and "WAVECAR present but unreadable" aren't indistinguishable
    # in the UI - a user staring at a folder that plainly has a WAVECAR in it needs to know it's
    # the second case, not go looking for a file that's already there.
    # irreps defaults to False (puntukas' own default): symmetry-labeling each band is real
    # per-band cost (get_symmetry over the structure) - a wide band range with irreps=True was
    # observed to be dramatically slower than the same range without it. Caller (ElectronicStructurePanel's
    # "Show symmetry labels" toggle) opts in explicitly.
    try:
        rows = output.get_orbital_data_for_two_spins(
            band_start, band_end, irreps=irreps, irrep_tol=irrep_tol, symprec=symprec)
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


def load_vasp_output_payload(
    directory: str, band_start: int, band_end: int, include_orbitals: bool = True, irreps: bool = False,
    irrep_tol: float = 1e-1, symprec: float = 1e-3) -> dict:
    resolved = pathlib.Path(directory).resolve()
    output = VaspOutput.from_directory(str(resolved))
    if include_orbitals:
        orbitals, orbitals_error = _orbitals_payload(output, band_start, band_end, irreps, irrep_tol, symprec)
    else:
        # Skips the WAVECAR read/per-band diagonalization entirely - CalculationSummaryPanel has
        # no use for orbital data and get_orbital_data_for_two_spins is real per-band cost this
        # caller shouldn't pay just because it shares a bridge with ElectronicStructurePanel.
        orbitals, orbitals_error = None, None
    return {
        "path": str(resolved),
        "gap": _band_gap_payload(output, str(resolved)),
        "orbitals": orbitals,
        "orbitals_error": orbitals_error,
        "summary": _summary_payload(output),
    }


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(
            "usage: vasp_output_load.py <calculation_directory> [band_start] [band_end] [include_orbitals] "
            "[irreps] [irrep_tol] [symprec]")

    directory = sys.argv[1]
    band_start = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    band_end = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    include_orbitals = sys.argv[4] != "0" if len(sys.argv) > 4 else True
    irreps = sys.argv[5] != "0" if len(sys.argv) > 5 else False
    irrep_tol = float(sys.argv[6]) if len(sys.argv) > 6 else 1e-1
    symprec = float(sys.argv[7]) if len(sys.argv) > 7 else 1e-3

    payload = load_vasp_output_payload(directory, band_start, band_end, include_orbitals, irreps, irrep_tol, symprec)
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
