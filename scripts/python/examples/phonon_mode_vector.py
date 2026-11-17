"""Read one dephonopy HDF5 mode, leaving the server tree untouched."""
from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True

import h5py
import numpy as np

from phonon_vibronic_analysis import publish


def analyse(embedded_dir, mode):
    with redirect_stdout(sys.stderr):
        from ase.io import read

        embedded_dir = Path(embedded_dir).resolve()
        atoms = read(embedded_dir / "POSCAR", format="vasp")
        masses = np.asarray(atoms.get_masses(), dtype=float)
        count = len(atoms)
        if count == 0 or not np.isfinite(masses).all() or np.any(masses <= 0):
            raise ValueError("Atomic masses must be finite and positive")
        if not isinstance(mode, int) or isinstance(mode, bool) or not 0 <= mode < 3 * count:
            raise ValueError("Mode index must be in [0, 3*n_atoms), including translations")
        with h5py.File(embedded_dir / "modes.h5", "r") as handle:
            vectors = handle["evecs"]
            frequencies = handle["freq_mev"]
            if vectors.shape != (3 * count, 3 * count) or frequencies.shape != (3 * count,):
                raise ValueError("HDF5 shapes do not match POSCAR")
            # dephonopy HDF5Eigenpairs writes mode-major mass-weighted rows, translations included.
            row = np.asarray(vectors[mode, :], dtype=float).reshape(count, 3)
            frequency = float(frequencies[mode])
        if not np.isfinite(row).all() or not np.isfinite(frequency):
            raise ValueError("Mode vector and frequency must be finite")
        # Same convention as the local script: physical displacement -> sqrt(m)*u -> unit norm.
        physical = row / np.sqrt(masses)[:, None]
        eigenvector = physical * np.sqrt(masses)[:, None]
        norm = np.linalg.norm(eigenvector)
        if not np.isfinite(norm) or norm <= 0:
            raise ValueError("Invalid phonon eigenvector norm")
        eigenvector = np.asarray(eigenvector / norm, dtype="<f4")
    return {"summary": {"schema": "ds.phonon_mode_vector.v1", "mode_index": mode,
                        "frequency_meV": frequency, "n_atoms": count,
                        "convention": "normalised_mass_weighted",
                        "input_paths": {"embedded_dir": str(embedded_dir)}},
            "eigenvector": eigenvector}


def write_result(result, directory):
    (directory / "mode.json").write_text(
        json.dumps(result["summary"], indent=2, allow_nan=False) + "\n", encoding="utf-8")
    np.save(directory / "eigenvector.npy", result["eigenvector"])


def main():
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise ValueError(message)

    parser = Parser(description=__doc__)
    parser.add_argument("--embedded-dir", required=True, type=Path)
    parser.add_argument("--mode", required=True, type=int)
    parser.add_argument("--out-dir", required=True, type=Path)
    try:
        args = parser.parse_args()
        embedded = args.embedded_dir.resolve()
        output = args.out_dir.resolve()
        system = embedded.parent.parent
        if output == system or system in output.parents or output in system.parents:
            raise ValueError("Output directory must not overlap the server system tree")
        result = analyse(embedded, args.mode)
        directory = publish(result, args.out_dir, writer=write_result)
        print(json.dumps({"ok": True, "out_dir": str(directory)}))
        return 0
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
