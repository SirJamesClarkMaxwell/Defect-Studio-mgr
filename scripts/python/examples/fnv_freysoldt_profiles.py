"""FNV lattice energy and planar profiles for FnvCorrectionBridge.

usage: fnv_freysoldt_profiles.py <defect LOCPOT> <bulk LOCPOT> <q> <epsilon> <fx,fy,fz> <encut_eV>
"""
from __future__ import annotations

from contextlib import redirect_stdout
import json
from pathlib import Path
import sys

try:
    import numpy as np
    from pymatgen.io.vasp import Incar, Locpot
    from pymatgen.analysis.defects.corrections.freysoldt import get_freysoldt_correction
except ImportError as exc:
    print(f"pymatgen_defects_not_installed: {exc}", file=sys.stderr)
    raise SystemExit(1)


def load(defect_path: str, bulk_path: str, q: int, eps: float,
         centre: list[float], encut: float) -> dict:
    if q == 0 or not np.isfinite(eps) or eps <= 0:
        raise ValueError("fnv_request_invalid: charge must be nonzero and dielectric positive and finite")
    if len(centre) != 3 or not np.all(np.isfinite(centre)) or not np.isfinite(encut):
        raise ValueError("fnv_request_invalid: expected three finite centre coordinates and a finite cutoff")
    if encut <= 0:
        incar_path = Path(defect_path).parent / "INCAR"
        encut = float(Incar.from_file(incar_path).get("ENCUT", 520)) if incar_path.is_file() else 520.0
    if not np.isfinite(encut) or encut <= 0:
        raise ValueError("fnv_request_invalid: ENCUT must be positive and finite")

    defect = Locpot.from_file(defect_path)
    bulk = Locpot.from_file(bulk_path)
    if defect.dim != bulk.dim:
        raise ValueError(f"locpot_mismatch: grid shapes {defect.dim} and {bulk.dim} differ")
    if not np.allclose(defect.structure.lattice.matrix, bulk.structure.lattice.matrix, atol=1e-4):
        raise ValueError("locpot_mismatch: defect and bulk lattice matrices differ")

    res = get_freysoldt_correction(q=q, dielectric=eps, defect_locpot=defect,
                                  bulk_locpot=bulk, defect_frac_coords=centre,
                                  energy_cutoff=encut)
    axes = []
    for a in range(3):
        pd = res.metadata["plot_data"][a]["pot_plot_data"]
        x = np.asarray(pd["x"])
        length = defect.structure.lattice.abc[a]
        # Undo pymatgen's defect-at-index-zero roll, exactly as in pymatgen_vs_sx.py.
        axbulk = centre[a] * length
        i = int(np.argmax(x > axbulk))
        roll = len(x) - i
        dv = np.roll(pd["dft_diff"], -roll)
        vr = np.roll(pd["Vr"], -roll)
        axes.append({"length": float(length), "deltaV": dv.tolist(), "modelPotential": vr.tolist()})
    return {
        "electrostatic": float(res.metadata["electrostatic"]),
        "energyCutoff": float(encut),
        "modelPotentialMean": float(np.mean(np.concatenate([axis["modelPotential"] for axis in axes]))),
        "axes": axes,
    }


def main() -> int:
    if len(sys.argv) != 7:
        print(__doc__.splitlines()[2], file=sys.stderr)
        return 1
    try:
        with redirect_stdout(sys.stderr):
            payload = load(sys.argv[1], sys.argv[2], int(sys.argv[3]), float(sys.argv[4]),
                           [float(value) for value in sys.argv[5].split(",")], float(sys.argv[6]))
        output = json.dumps(payload, allow_nan=False)
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
