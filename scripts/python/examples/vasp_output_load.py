from __future__ import annotations

import json
import pathlib
import sys

import numpy as np

try:
    from puntukas.vasp import Outcar, VaspOutput, VasprunData
except ImportError as exc:
    print(json.dumps({"error": "puntukas_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)


def _band_gap_payload(output) -> dict | None:
    # The new aggregate reader exposes eigenvalues/occupations publicly and falls back from
    # vasprun.xml to EIGENVAL. Preserve puntukas' global all-spin/all-kpoint band-edge convention.
    eigenvalues = output.get_eigenvalues()
    occupations = output.get_occupations()
    if eigenvalues is None or occupations is None:
        return None
    occupied = ~np.isclose(occupations, 0)
    unoccupied = np.isclose(occupations, 0)
    if not occupied.any() or not unoccupied.any():
        return None
    homo = float(eigenvalues[occupied].max())
    lumo = float(eigenvalues[unoccupied].min())
    return {"bandgap": lumo - homo, "homo": homo, "lumo": lumo}


def _summary_payload(output, directory: pathlib.Path) -> dict:
    # Each field independently try/excepted - a partial/older OUTCAR or vasprun.xml can have some
    # of these and not others (e.g. no WAVECAR needed here at all, unlike orbitals below), and one
    # missing field shouldn't blank out the rest of the summary.
    summary: dict = {}

    try:
        vasprun = VasprunData.from_file(directory / "vasprun.xml")
    except (OSError, ValueError):
        vasprun = None
    try:
        outcar = Outcar.from_file(directory / "OUTCAR")
    except (OSError, ValueError):
        outcar = None

    summary["energy_trend"] = (
        [float(step[-1]) for step in vasprun.energies if len(step)]
        if vasprun is not None else None
    )

    try:
        summary["final_energy"] = float(output.etot)
    except (AttributeError, TypeError):
        summary["final_energy"] = None

    summary["cpu_time"] = float(output.cpu_time) if output.cpu_time is not None else None
    for field in ("user_time", "system_time", "elapsed_time"):
        value = getattr(outcar, field, None)
        summary[field] = float(value) if value is not None else None

    try:
        drift = output.drift
        summary["total_drift"] = [float(component) for component in drift] if drift is not None else None
    except (AttributeError, TypeError):
        summary["total_drift"] = None

    try:
        summary["nelect"] = float(output.nelect)
    except (AttributeError, TypeError):
        summary["nelect"] = None
    try:
        ispin = vasprun.parameters.get("ISPIN") if vasprun is not None else None
        summary["ispin"] = int(ispin) if ispin is not None else None
    except (AttributeError, TypeError):
        summary["ispin"] = None

    try:
        pressure = output.get_pressure()
        summary["pressure"] = float(pressure) if pressure is not None else None
    except (AttributeError, TypeError):
        summary["pressure"] = None
    try:
        stress = vasprun.get_stress_tensor() if vasprun is not None else None
        summary["stress_tensor"] = [[float(v) for v in row] for row in stress] if stress is not None else None
    except (AttributeError, TypeError):
        summary["stress_tensor"] = None

    try:
        atoms = output.atoms
        sym = atoms.get_symmetry() if atoms is not None else None
        if sym is None:
            raise ValueError("No structure available for symmetry analysis")
        summary["space_group_symbol"] = str(sym.international_symbol)
        summary["space_group_number"] = int(sym.spacegroup_number)
        summary["point_group_symbol"] = str(sym.pointgroup)
        summary["point_group_schoenflies"] = str(sym.schoenflies)
    except (ImportError, AttributeError, TypeError, ValueError, RuntimeError):
        summary["space_group_symbol"] = None
        summary["space_group_number"] = None
        summary["point_group_symbol"] = None
        summary["point_group_schoenflies"] = None

    return summary


def _orbitals_payload(
    output, band_start: int, band_end: int, irreps: bool = False, irrep_tol: float = 1e-1,
    symprec: float = 1e-3) -> tuple[list[dict] | None, str | None]:
    # orbital_info raises FileNotFoundError if WAVECAR/vaspwave.h5 is absent or unreadable. The
    # aggregate reader records parse errors through check(), so those cases remain distinguishable.
    # ireps defaults to False (puntukas' own default): symmetry-labeling each band is real
    # per-band cost (symmetry analysis over the structure) - a wide band range with ireps=True was
    # observed to be dramatically slower than the same range without it. Caller (ElectronicStructurePanel's
    # "Show symmetry labels" toggle) opts in explicitly.
    # band_start/band_end are VASP's own 1-based, inclusive band numbers (matching OUTCAR/EIGENVAL
    # - what a user actually cross-checks against), but orbital_info indexes the WAVECAR's band
    # array 0-based and its "nr" column is that same raw index. Only band_start
    # needs the -1 shift going in - range(start, end)'s exclusive end already happens to line up
    # with an inclusive 1-based band_end. "nr" gets +1 coming back out so the reported band numbers
    # match VASP's, not the WAVECAR array position.
    try:
        rows = output.orbital_info(
            max(band_start - 1, 0), band_end, ireps=irreps, irep_tol=irrep_tol, symprec=symprec)
    except FileNotFoundError:
        for status in output.check():
            if status.name == "wavecar" and status.error is not None:
                return None, f"WAVECAR/vaspwave.h5 present but unreadable ({status.error}) - " \
                    "possibly corrupted or incompletely transferred (seen on network drives)"
        return None, None

    names = rows.dtype.names or ()
    two_channels = "e(up)" in names
    has_irrep = "irrep(up)" in names if two_channels else "irrep" in names
    records = []
    for row in rows:
        if two_channels:
            up = {
                "energy": float(row["e(up)"]),
                "occupation": float(row["occ(up)"]),
                "localization": float(row["loc(up)"]),
                "irrep": str(row["irrep(up)"]) if has_irrep else None,
            }
            down = {
                "energy": float(row["e(down)"]),
                "occupation": float(row["occ(down)"]),
                "localization": float(row["loc(down)"]),
                "irrep": str(row["irrep(down)"]) if has_irrep else None,
            }
        else:
            up = {
                "energy": float(row["energy"]),
                "occupation": float(row["occ"]),
                "localization": float(row["loc"]),
                "irrep": str(row["irrep"]) if has_irrep else None,
            }
            down = {"energy": up["energy"], "occupation": 0.0, "localization": 0.0, "irrep": None}
        records.append({
            "band": int(row["nr"]) + 1,
            "up": up,
            "down": down,
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
        # no use for orbital data and orbital_info is real per-band cost this
        # caller shouldn't pay just because it shares a bridge with ElectronicStructurePanel.
        orbitals, orbitals_error = None, None
    return {
        "path": str(resolved),
        "gap": _band_gap_payload(output),
        "orbitals": orbitals,
        "orbitals_error": orbitals_error,
        "summary": _summary_payload(output, resolved),
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
