"""Import cluster dephonopy HR sticks without writing to the server or loading modes.h5."""
from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import json
import math
from pathlib import Path, PurePosixPath
import re
import sys

sys.dont_write_bytecode = True

import numpy as np
from scipy.constants import atomic_mass, electron_volt, hbar, physical_constants
from phonon_vibronic_analysis import RESULT_UNITS, build_spectra, parse_mult, publish, read_toten
from zpl_formula import evaluate, used_variables


def map_cluster_path(value, hr_dir):
    """Try longest component suffix first, under each ancestor of the mounted HR directory."""
    parts = PurePosixPath(value.replace("\\", "/")).parts
    if ".." in parts:
        return None
    for start in range(len(parts)):
        suffix = parts[start:]
        if not suffix or suffix[0] == "/" or ":" in suffix[0]:
            continue
        for ancestor in (hr_dir, *hr_dir.parents):
            candidate = ancestor.joinpath(*suffix)
            if candidate.is_file():
                return candidate.resolve()
    return None


def read_column(path, count, strings=False):
    values = np.loadtxt(path, dtype=str if strings else float, ndmin=1)
    if values.shape != (count,):
        raise ValueError(f"Expected {count} rows with one value in {path}")
    if not strings and not np.isfinite(values).all():
        raise ValueError(f"Expected finite values in {path}")
    return values


def analyse(hr_dir, sigma, gamma, zpl_exp=None, zpl_formula=None, label=None, sigma_max=None):
    for name, value in (("sigma", sigma), ("gamma", gamma)):
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"{name} must be finite and positive (meV)")
    if sigma_max is not None and (not math.isfinite(sigma_max) or sigma_max < sigma):
        raise ValueError("sigma-max must be finite and at least sigma (meV)")
    if zpl_exp is not None and (not math.isfinite(zpl_exp) or zpl_exp <= 0):
        raise ValueError("Experimental ZPL must be finite and positive (eV)")
    hr_dir = Path(hr_dir).resolve()
    size_dir = hr_dir.parent
    if size_dir.parent.name != "spectral-function":
        raise ValueError(f"Expected spectral-function/AxBxC/variant layout: {hr_dir}")
    mult = parse_mult(size_dir.name)
    system = size_dir.parent.parent
    embedded = system / "embedded" / size_dir.name
    variant = hr_dir.name
    method = re.sub(r"_cutoff[\d.]+A$", "", variant)
    log = (hr_dir / "hr_factors.log").read_text(encoding="utf-8", errors="replace")
    mapped = {}
    for state, key in (("gs", "final_path"), ("es", "init_path")):
        match = re.search(rf"\b{key}:\s*([^\r\n]+)", log)
        mapped[state] = map_cluster_path(match[1].strip().strip("\"'"), hr_dir) if match else None
    paths = {"hr_dir": hr_dir, **{f"{state}_dir": path.parent if path else None
                                for state, path in mapped.items()}}
    energies = {name: read_toten(paths[key]) if paths[key] and (paths[key] / "OUTCAR").is_file() else None
                for name, key in (("E_gs", "gs_dir"), ("E_es", "es_dir"))}
    computed = (energies["E_es"] - energies["E_gs"]
                if all(value is not None for value in energies.values()) else None)
    zpls = {"computed": computed, "computed_spin_purified": None, "experimental": zpl_exp, "formula": None}
    formula_info = None
    if zpl_formula is not None:
        names = used_variables(zpl_formula)
        zpls["formula"] = evaluate(zpl_formula, energies)
        formula_info = {"text": zpl_formula, "variables_eV": {name: energies[name] for name in names},
                        "value_eV": zpls["formula"]}
    if any(value is not None and (not math.isfinite(value) or value <= 0) for value in zpls.values()):
        raise ValueError("ZPL energies must be finite and positive for emission")

    with redirect_stdout(sys.stderr):
        from ase.io import read
        from dephonopy.hr_factors.io import read_atomic_displacements
        from dephonopy.utils.units import Units

        candidates = (embedded / "POSCAR", system / "dispersion" / "SPOSCAR", system / "dispersion" / "POSCAR")
        structure_path = next((path for path in candidates if path.is_file()), None)
        if structure_path is None:
            raise ValueError(f"No embedded POSCAR or dispersion SPOSCAR/POSCAR for {hr_dir}")
        atoms = read(structure_path, format="vasp")
        n_atoms = len(atoms)
        n_modes = 3 * n_atoms
        if n_modes <= 3:
            raise ValueError("Expected at least two atoms")
        frequencies = np.r_[np.zeros(3), read_column(hr_dir / "freq_mev.dat", n_modes - 3)]
        s_k = np.r_[np.zeros(3), read_column(hr_dir / "hr_factors.dat", n_modes - 3)]
        fc_displ = read_column(hr_dir / "fc_displ.dat", n_modes - 3)
        irreps = np.r_[np.full(3, "???"), read_column(hr_dir / "irreps.dat", n_modes - 3, strings=True)]
        if np.any(s_k[frequencies > 0] < 0):
            raise ValueError("Positive-frequency Huang-Rhys factors must be nonnegative")
        n_eff = np.full(n_modes, np.nan)
        if (embedded / "iprs.dat").is_file():
            n_eff = read_column(embedded / "iprs.dat", n_modes)
            if np.any(n_eff <= 0):
                raise ValueError("Participation counts must be positive")
        if (embedded / "freq.dat").is_file():
            with (embedded / "freq.dat").open(encoding="utf-8") as handle:
                header = handle.readline()
            units = re.search(r"frequencies\s*\(([^)]+)\)", header, re.IGNORECASE)
            units = units[1].lower() if units else "mev"
            factor = (Units.convert_frequencies(np.array([1.0]), "mev")[0] /
                      Units.convert_frequencies(np.array([1.0]), units)[0])
            frequencies[:3] = read_column(embedded / "freq.dat", n_modes)[:3] * factor
        is_translation = np.arange(n_modes) < 3
        q = np.zeros(n_modes)
        positive = (frequencies[3:] > 0)
        q[3:][positive] = np.sign(fc_displ[positive]) * np.sqrt(
            2 * hbar**2 * s_k[3:][positive] /
            (frequencies[3:][positive] * 1e-3 * electron_volt * atomic_mass * 1e-20))
        masses = np.asarray(atoms.get_masses(), dtype=float)
        if not np.isfinite(masses).all() or np.any(masses <= 0):
            raise ValueError("Atomic masses must be finite and positive")
        maximum, delta_q = None, None
        if all(mapped.values()):
            displacement, gs, es = read_atomic_displacements(str(mapped["gs"]), str(mapped["es"]), verbose=False)
            if (gs.get_chemical_symbols() != es.get_chemical_symbols()
                    or not np.allclose(gs.get_cell(), es.get_cell(), rtol=0, atol=1e-5)
                    or not np.allclose(gs.get_masses(), es.get_masses(), rtol=0, atol=1e-5)):
                raise ValueError("Ground and excited structures must have the same ordered species, cell and masses")
            displacement = np.asarray(displacement, dtype=float)
            displacement -= np.average(displacement, axis=0, weights=gs.get_masses())
            displacement *= physical_constants["Bohr radius"][0] * 1e10
            if not np.isfinite(displacement).all():
                raise ValueError("Non-finite atomic displacement")
            maximum = float(np.linalg.norm(displacement, axis=1).max())
            delta_q = float(np.sqrt(np.sum(np.asarray(gs.get_masses())[:, None] * displacement**2)))
        s_total = float(s_k[3:][frequencies[3:] > 0].sum())
        spectral_function, pl = build_spectra(frequencies, np.where(frequencies > 0, s_k, 0),
                                             is_translation, s_total, zpls, sigma, gamma, sigma_max)

    cutoff = re.search(r"cutoff radius\s*\((?:auto|user)\):\s*([-+\d.eE]+)\s*A", log)
    cutoff = float(cutoff[1]) if cutoff else None
    if cutoff is not None and (not math.isfinite(cutoff) or cutoff <= 0):
        raise ValueError("Invalid HR cutoff in log")
    try:
        fc_mult = parse_mult(system.name)
    except ValueError:
        fc_mult = None
    imaginary = np.flatnonzero((frequencies < -0.5) & ~is_translation)
    summary = {"schema": "ds.phonon_vibronic.v1", "n_atoms": n_atoms, "n_modes": n_modes,
               "source": "server", "has_eigenvectors": False, "supercell": list(mult),
               "hr_method": method, "hr_cutoff_A": cutoff,
               "server": {"hr_dir": str(hr_dir), "hr_factors_mtime": (hr_dir / "hr_factors.dat").stat().st_mtime},
               "label": label if label is not None else f"{system.name} {size_dir.name} {variant}",
               "zpl_formula": formula_info, "zpl_eV": zpls,
               "embedding": {"mult": list(mult)} if fc_mult is not None and mult != fc_mult else None,
               "translations": {"indices": [0, 1, 2], "freq_meV": frequencies[:3].tolist()},
               "imaginary_modes": [{"index": int(i), "freq_meV": float(frequencies[i])} for i in imaginary],
               "stable": len(imaginary) == 0, "s_total": s_total, "debye_waller": math.exp(-s_total),
               "max_displacement_A": maximum, "delta_Q_amu05_A": delta_q,
               "sigma_meV": sigma, "sigma_max_meV": sigma_max, "gamma_meV": gamma,
               "input_paths": {key: str(path) if path else None for key, path in paths.items()},
               "units": RESULT_UNITS,
               "approximation": f"dephonopy calc_hr_factors ({method}), harmonic displaced oscillators, T=0"}
    return {"summary": summary, "structure": {"symbols": atoms.get_chemical_symbols(),
            "positions_A": atoms.get_positions().tolist(), "cell_A": atoms.get_cell().tolist(),
            "masses_amu": masses.tolist()}, "frequencies": frequencies, "irreps": irreps,
            "n_eff": n_eff, "s_k": s_k, "q": q, "is_translation": is_translation,
            "spectral_function": spectral_function, "pl": pl}


def main():
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise ValueError(message)

    parser = Parser(description=__doc__)
    for name in ("hr-dir", "out-dir"):
        parser.add_argument(f"--{name}", required=True, type=Path)
    for name in ("sigma", "gamma"):
        parser.add_argument(f"--{name}", required=True, type=float)
    parser.add_argument("--zpl-exp", type=float)
    parser.add_argument("--zpl-formula")
    parser.add_argument("--label")
    parser.add_argument("--sigma-max", type=float)
    try:
        args = vars(parser.parse_args())
        out_dir = args.pop("out_dir")
        hr_dir = args["hr_dir"].resolve()
        # publish() may replace its entire output: never allow any write inside the server system.
        output = out_dir.resolve()
        system = hr_dir.parent.parent.parent
        if output == system or system in output.parents:
            raise ValueError("Output directory must be outside the server system tree")
        result = analyse(**args)
        out_dir = publish(result, out_dir)
        print(json.dumps({"ok": True, "out_dir": str(out_dir), "s_total": result["summary"]["s_total"],
                          "stable": result["summary"]["stable"]}))
        return 0
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
