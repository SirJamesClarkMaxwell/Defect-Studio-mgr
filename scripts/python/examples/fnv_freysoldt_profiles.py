"""FNV lattice energy and planar profiles for FnvCorrectionBridge.

usage: fnv_freysoldt_profiles.py <defect LOCPOT> <bulk LOCPOT> <q> <epsilon> <fx,fy,fz> <encut_eV>
"""
from __future__ import annotations

from contextlib import redirect_stdout
import json
import re
from pathlib import Path
import sys

try:
    import numpy as np
    from pymatgen.io.vasp import Locpot
    from pymatgen.analysis.defects.corrections.freysoldt import get_freysoldt_correction
except ImportError as exc:
    print(f"pymatgen_defects_not_installed: {exc}", file=sys.stderr)
    raise SystemExit(1)


def read_encut(incar_path: Path) -> float:
    # Not pymatgen's Incar: hand-written INCARs carry comments in any encoding (the Okeanos ones have
    # a truncated UTF-8 Polish letter), and only one number is needed.
    text = incar_path.read_bytes().decode("utf-8", errors="replace")
    match = re.search(r"^\s*ENCUT\s*=\s*([0-9.+\-eE]+)", text, flags=re.MULTILINE | re.IGNORECASE)
    return float(match.group(1)) if match else 520.0


def pymatgen_roll(x: np.ndarray, axbulk: float, length: float) -> tuple[int, float]:
    """The roll perform_pot_corr applies (first grid point strictly past the defect -> index 0,
    no roll for a defect at 0) and how far that grid point sits from the defect, in A.
    pymatgen builds V_model with the defect at index 0, so after undoing the roll the model peaks
    `offset` (0 .. one grid step) away from the true centre; shift_periodic moves it back."""
    if axbulk < 0:
        axbulk += length
    elif axbulk > length:
        axbulk -= length
    if not axbulk:
        return 0, 0.0
    i = next((k for k in range(len(x)) if axbulk < x[k]), len(x) - 1)
    return len(x) - i, float(x[i] - axbulk)


def shift_periodic(values: np.ndarray, offset: float, length: float) -> np.ndarray:
    """values(x + offset) for a smooth periodic profile, by a Fourier phase shift."""
    freq = np.fft.fftfreq(len(values), d=length / len(values))
    return np.fft.ifft(np.fft.fft(values) * np.exp(2j * np.pi * freq * offset)).real


def load(defect_path: str, bulk_path: str, q: int, eps: float,
         centre: list[float], encut: float) -> dict:
    if q == 0 or not np.isfinite(eps) or eps <= 0:
        raise ValueError("fnv_request_invalid: charge must be nonzero and dielectric positive and finite")
    if len(centre) != 3 or not np.all(np.isfinite(centre)) or not np.isfinite(encut):
        raise ValueError("fnv_request_invalid: expected three finite centre coordinates and a finite cutoff")
    if encut <= 0:
        incar_path = Path(defect_path).parent / "INCAR"
        encut = read_encut(incar_path) if incar_path.is_file() else 520.0
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
        roll, offset = pymatgen_roll(x, centre[a] * length, length)
        dv = np.roll(pd["dft_diff"], -roll)
        vr = shift_periodic(np.roll(pd["Vr"], -roll), offset, length)
        axes.append({"length": float(length), "deltaV": dv.tolist(), "modelPotential": vr.tolist()})
    return {
        "electrostatic": float(res.metadata["electrostatic"]),
        "energyCutoff": float(encut),
        "modelPotentialMean": float(np.mean(np.concatenate([axis["modelPotential"] for axis in axes]))),
        "axes": axes,
    }


def selftest() -> None:
    length, n = 14.0, 180
    x = np.arange(n) * length / n
    gauss = lambda c: sum(np.exp(-((x - c + k * length) ** 2) / 0.5) for k in (-1, 0, 1))
    # pymatgen's model sits on grid point 91 for a defect at 6.95 A; moving it back lands on 6.95
    roll, offset = pymatgen_roll(x, 6.95, length)
    assert roll == n - 90 and abs(offset - (x[90] - 6.95)) < 1e-12
    assert np.abs(shift_periodic(gauss(x[90]), offset, length) - gauss(6.95)).max() < 1e-9
    assert pymatgen_roll(x, 0.0, length) == (0, 0.0)
    assert pymatgen_roll(x, x[-1] + 0.01, length)[0] == 1  # past the last point: pymatgen rolls by 1
    print("ok")


def main() -> int:
    if sys.argv[1:] == ["--selftest"]:
        selftest()
        return 0
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
