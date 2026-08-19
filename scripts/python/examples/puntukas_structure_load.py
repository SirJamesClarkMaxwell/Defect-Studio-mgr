from __future__ import annotations

import json
import pathlib
import sys

try:
    from puntukas.vasp import Poscar
except ImportError as exc:
    print(json.dumps({"error": "puntukas_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)

# ponytail: puntukas.atoms.base.AtomsBase.get_positions()/get_cell() declare units="angstrom"
# and call U.from_angstrom(...), but puntukas.vasp.poscar.poscar.py's atoms_from_data() stores
# positions/cell already converted to atomic units (Units.convert_vasp_distances(..., "au")) -
# from_angstrom() on data that isn't angstrom is a no-op, so both come out in Bohr. Fractional/
# scaled positions are a dimensionless ratio, unaffected. Fix locally here rather than upstream
# in punktukas-tools (see docs/work/project/TODO.md hotfix note). Extract to
# scripts/python/common/ once a second puntukas bridge (WAVECAR/EIGENVAL/OUTCAR, T08.6) needs it.
_BOHR_TO_ANGSTROM = 0.529177210903


def load_structure_payload(raw_path: str) -> dict:
    input_path = pathlib.Path(raw_path).resolve()
    atoms = Poscar(str(input_path)).atoms

    symbols = atoms.get_chemical_symbols()
    fractional = atoms.get_scaled_positions().tolist()
    cartesian = (atoms.get_positions() * _BOHR_TO_ANGSTROM).tolist()
    charges = atoms.get_initial_charges().tolist() if atoms.has("initial_charges") else None
    magmoms = atoms.get_initial_magnetic_moments().tolist() if atoms.has("initial_magmoms") else None

    return {
        "path": str(input_path),
        "reduced_formula": atoms.get_chemical_formula(mode="reduce"),
        "lattice": [[value * _BOHR_TO_ANGSTROM for value in row] for row in atoms.cell.tolist()],
        "sites": [
            {
                "element": symbols[i],
                "fractional": fractional[i],
                "cartesian": cartesian[i],
                # puntukas.vasp.Poscar's parser reads the Selective-Dynamics header flag but
                # discards the per-atom T/T/T columns (see puntukas/vasp/poscar/poscar.py),
                # so this is always unavailable via this loader.
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
