from __future__ import annotations

import json
import pathlib
import sys

from pymatgen.core import Structure


def site_property(site, name: str):
    value = site.properties.get(name)
    if value is None:
        return None
    if hasattr(value, "tolist"):
        return value.tolist()
    return value


def site_float_property(site, name: str):
    value = site_property(site, name)
    return float(value) if value is not None else None


def site_occupancy(site) -> float:
    if hasattr(site, "species"):
        return float(sum(site.species.values()))
    return 1.0


def load_structure_payload(raw_path: str) -> dict:
    input_path = pathlib.Path(raw_path).resolve()
    structure = Structure.from_file(input_path)
    return {
        "path": str(input_path),
        "reduced_formula": structure.composition.reduced_formula,
        "lattice": structure.lattice.matrix.tolist(),
        "sites": [
            {
                "element": site.specie.symbol,
                "fractional": site.frac_coords.tolist(),
                "cartesian": site.coords.tolist(),
                "selective_dynamics": site_property(site, "selective_dynamics"),
                "charge": site_float_property(site, "charge"),
                "magmom": site_float_property(site, "magmom"),
                "occupancy": site_occupancy(site),
            }
            for site in structure.sites
        ],
    }


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: pymatgen_structure_load.py <structure_path> [structure_path ...]")

    payloads = [load_structure_payload(raw_path) for raw_path in sys.argv[1:]]
    payload = payloads[0] if len(payloads) == 1 else {"structures": payloads}
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
