"""Compute a bulk phonon field in a supercell of the phonopy primitive cell."""
from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True

import numpy as np
from phonon_vibronic_analysis import publish


def supercell_size(q, requested="auto", max_multiple=24, in_plane=False):
    q = np.asarray(q, dtype=float)
    if max_multiple < 1 or q.shape != (3,) or not np.isfinite(q).all():
        raise ValueError("Invalid q or max-multiple")
    suggestion = []
    for axis, value in enumerate(q):
        limit = 1 if in_plane and axis == 2 else max_multiple
        suggestion.append(next((n for n in range(1, limit + 1)
                                if abs(value * n - round(value * n)) <= 1e-6), None))
    suggestion = None if None in suggestion else suggestion
    if requested == "auto":
        size = suggestion or [1 if abs(v) <= 1e-6 or (in_plane and i == 2) else
                              min(max_multiple, int(np.ceil(1 / abs(v))))
                              for i, v in enumerate(q)]
    else:
        if not re.fullmatch(r"[1-9][0-9]*[xX][1-9][0-9]*[xX][1-9][0-9]*", requested):
            raise ValueError("Supercell must be auto or NxMxK (positive integers)")
        size = [int(n) for n in re.split("[xX]", requested)]
        if in_plane:
            size[2] = 1
    commensurate = bool(np.all(np.abs(q * size - np.rint(q * size)) <= 1e-6))
    return size, commensurate, suggestion


def normalize(field):
    real, imag = field.real, field.imag
    a = np.sum(real * real, axis=1)
    b = np.sum(real * imag, axis=1)
    c = np.sum(imag * imag, axis=1)
    maximum = np.sqrt(np.max((a + c + np.hypot(a - c, 2 * b)) / 2))
    if not np.isfinite(maximum) or maximum <= 0:
        raise ValueError("Empty or non-finite modulation")
    return field / maximum


def mode_data(phonon, q, band, supercell="auto", in_plane=False, max_multiple=24,
              degenerate_tol_mev=0.5, energy_to_mev=1000.0):
    q = np.asarray(q, dtype=float)
    if not np.isfinite(degenerate_tol_mev) or degenerate_tol_mev < 0:
        raise ValueError("Invalid degeneracy tolerance")
    size, commensurate, suggestion = supercell_size(q, supercell, max_multiple, in_plane)
    phonon.run_qpoints([q])
    frequencies = np.asarray(phonon.qpoints.frequencies[0]) * energy_to_mev
    if not 0 <= band < len(frequencies) or not np.isfinite(frequencies).all():
        raise ValueError("Invalid band or non-finite frequencies")
    gaps = np.abs(frequencies - frequencies[band])
    gaps[band] = np.inf
    partner = int(np.argmin(gaps)) if np.min(gaps) <= degenerate_tol_mev else None
    bands = [band] + ([] if partner is None else [partner])
    # Phonopy applies the atomic phase and mass division; dimension is primitive-relative.
    modulation = phonon.run_modulations(size, [[q, b, 1.0, 0.0] for b in bands])
    fields, cell = modulation.get_modulations_and_supercell()
    fields = [normalize(f) for f in fields]
    label = "q=(" + ", ".join(f"{v:.6g}" for v in q) + f") gałąź {band}"
    vibration = {"schema": "ds.phonon_mode.v1", "label": label,
                 "frequency_meV": float(frequencies[band]), "q": q.tolist(),
                 "q_basis": "primitive_reciprocal_reduced", "band": band, "n_bands": len(frequencies),
                 "supercell": size, "commensurate": commensurate, "suggested_supercell": suggestion,
                 "symbols": list(cell.symbols), "masses_amu": cell.masses.tolist(),
                 "cell_angstrom": cell.cell.tolist(), "positions_angstrom": cell.positions.tolist(),
                 "real_angstrom": fields[0].real.tolist(), "imag_angstrom": fields[0].imag.tolist(),
                 "partner": None}
    if partner is not None:
        vibration["partner"] = {"band": partner, "frequency_meV": float(frequencies[partner]),
                                "real_angstrom": fields[1].real.tolist(), "imag_angstrom": fields[1].imag.tolist()}
    return {"summary": {"schema": vibration["schema"], "label": label}, "vibration": vibration}


def analyse(fc_dir, q, band, supercell="auto", in_plane=False, max_multiple=24,
            degenerate_tol_mev=0.5, no_symmetrize=False):
    fc_dir = Path(fc_dir).resolve()
    if not fc_dir.is_dir():
        raise ValueError(f"Input directory does not exist: {fc_dir}")
    with redirect_stdout(sys.stderr):
        from dephonopy.force_constants import ForceConstants

        fc = ForceConstants(str(fc_dir))
        if not no_symmetrize:
            fc.ensure_symmetry()
        result = mode_data(fc.Phonopy(unit_cell=fc.atoms), q, band, supercell, in_plane,
                           max_multiple, degenerate_tol_mev)
    result["summary"]["input_paths"] = {"fc_dir": str(fc_dir)}
    return result


def write_result(result, directory):
    directory = Path(directory)
    vibration = result["vibration"]
    vibration.update({k: result["summary"][k] for k in ("input_paths", "inputs_relative")
                      if k in result["summary"]})
    for name, data in (("summary.json", result["summary"]), ("vibration.json", vibration)):
        (directory / name).write_text(json.dumps(data, indent=2, ensure_ascii=False, allow_nan=False) + "\n",
                                      encoding="utf-8")
    # Keep the exact modulation atom order, including repeated species runs.
    runs = []
    for symbol in vibration["symbols"]:
        if runs and runs[-1][0] == symbol:
            runs[-1] = (symbol, runs[-1][1] + 1)
        else:
            runs.append((symbol, 1))
    lines = ["Bulk phonon mode", "1.0"]
    lines += [" ".join(f"{v:.16g}" for v in row) for row in vibration["cell_angstrom"]]
    lines += [" ".join(s for s, _ in runs), " ".join(str(n) for _, n in runs), "Cartesian"]
    lines += [" ".join(f"{v:.16g}" for v in row) for row in vibration["positions_angstrom"]]
    (directory / "POSCAR").write_text("\n".join(lines) + "\n", encoding="utf-8")


def self_test():
    from phonopy import Phonopy
    from phonopy.structure.atoms import PhonopyAtoms

    cell = PhonopyAtoms(symbols=["Si", "Si"], masses=[1., 4.], cell=np.eye(3) * 4,
                        scaled_positions=[[0, 0, 0], [.5, 0, 0]])
    ph = Phonopy(cell, supercell_matrix=[1, 1, 1], primitive_matrix=np.eye(3))
    fc = np.zeros((2, 2, 3, 3))
    fc[0, 0] = fc[1, 1] = np.diag([1., 2., 3.])
    fc[0, 1] = fc[1, 0] = -fc[0, 0]
    ph.force_constants = fc
    gamma = mode_data(ph, [0, 0, 0], 0)["vibration"]
    np.testing.assert_allclose(gamma["real_angstrom"][0], gamma["real_angstrom"][1], atol=1e-7)
    for q, n in ((.5, 2), (1 / 3, 3)):
        value = mode_data(ph, [q, 0, 0], 5)["vibration"]
        assert value["supercell"] == [n, 1, 1] and value["commensurate"]
        # Two repeats have the same complex vector under the selected supercell translation.
        doubled = mode_data(ph, [q, 0, 0], 5, f"{2*n}x1x1")["vibration"]
        u = np.array(doubled["real_angstrom"]) + 1j * np.array(doubled["imag_angstrom"])
        positions = np.array(doubled["positions_angstrom"])
        for atom, position in enumerate(positions):
            target = position + np.array([4*n, 0, 0])
            indices = np.flatnonzero(np.linalg.norm(positions - target, axis=1) < 1e-7)
            if len(indices):
                np.testing.assert_allclose(u[atom], u[indices[0]], atol=1e-7)
    value = mode_data(ph, [.123, 0, 0], 5)["vibration"]
    assert not value["commensurate"] and value["suggested_supercell"] is None
    assert value["supercell"] == [9, 1, 1]
    fragment = mode_data(ph, [1/3, 0, 0], 5, "2x1x1")["vibration"]
    assert not fragment["commensurate"] and fragment["suggested_supercell"] == [3, 1, 1]
    assert supercell_size([0, 0, .5], in_plane=True)[1:] == (False, None)
    optical = mode_data(ph, [0, 0, 0], 5)["vibration"]
    ph.run_qpoints([[0, 0, 0]], with_eigenvectors=True)
    eigen = ph.qpoints.eigenvectors[0, :, 5].reshape(2, 3)
    expected = normalize(eigen / np.sqrt(cell.masses[:, None]))
    actual = np.array(optical["real_angstrom"]) + 1j * np.array(optical["imag_angstrom"])
    np.testing.assert_allclose(np.abs(actual), np.abs(expected), atol=1e-7)
    for invalid in ("0x1x1", "2x3", "-1x2x3", "1x2x3x4"):
        try:
            supercell_size([0, 0, 0], invalid)
        except ValueError:
            pass
        else:
            raise AssertionError(invalid)
    print(json.dumps({"ok": True, "self_test": "passed"}))


def main():
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise ValueError(message)

    parser = Parser(description=__doc__)
    parser.add_argument("--fc-dir", type=Path)
    parser.add_argument("--q", type=float, nargs=3)
    parser.add_argument("--band", type=int)
    parser.add_argument("--supercell", default="auto")
    parser.add_argument("--in-plane", action="store_true")
    parser.add_argument("--max-multiple", type=int, default=24)
    parser.add_argument("--degenerate-tol-mev", type=float, default=.5)
    parser.add_argument("--no-symmetrize", action="store_true")
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--self-test", action="store_true")
    try:
        args = vars(parser.parse_args())
        if args.pop("self_test"):
            self_test()
            return 0
        out_dir = args.pop("out_dir")
        if any(args[k] is None for k in ("fc_dir", "q", "band")) or out_dir is None:
            raise ValueError("--fc-dir, --q, --band and --out-dir are required")
        result = analyse(**args)
        output = publish(result, out_dir, writer=write_result)
        print(json.dumps({"ok": True, "out_dir": str(output)}))
        return 0
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
