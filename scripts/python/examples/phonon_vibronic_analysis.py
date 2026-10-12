"""Gamma phonons and T=0 displaced-oscillator PL; no input files are written."""
from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import json
import math
from pathlib import Path
import re
import shutil
import sys
from types import SimpleNamespace
from uuid import uuid4

# Keep scientific-package imports from writing into the read-only input/runtime trees.
sys.dont_write_bytecode = True

import numpy as np
from scipy.constants import atomic_mass, electron_volt, hbar, physical_constants
from scipy.linalg import eigh


def read_toten(directory):
    energy = None
    pattern = re.compile(r"free\s+energy\s+TOTEN\s*=\s*([-+\d.EeDd]+)")
    with (Path(directory) / "OUTCAR").open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = pattern.search(line)
            if match:
                energy = float(match[1].replace("D", "E").replace("d", "e"))
    if energy is None or not math.isfinite(energy):
        raise ValueError(f"No finite TOTEN in {directory}/OUTCAR")
    return energy


def mode_factors(frequencies, eigenvectors, masses, displacement):
    """S_k in SI units, with signed phonon energies and frequency-selected translations."""
    translations = np.argsort(np.abs(frequencies), kind="stable")[:3]
    q = np.einsum("kij,ij->k", eigenvectors, displacement * np.sqrt(masses)[:, None])
    s = frequencies * q**2 * (1e-3 * electron_volt * atomic_mass * 1e-20 / (2 * hbar**2))
    s[translations] = 0.0
    return translations, q, s


def parse_mult(value):
    if not re.fullmatch(r"[1-9]\d*x[1-9]\d*x[1-9]\d*", value):
        raise ValueError("mult must contain three positive integers, e.g. 12x12x1")
    return tuple(map(int, value.split("x")))


def origin_shift(defect, bulk, primitive):
    """Score species-preserving nearest-site matches; return t modulo the primitive cell."""
    from ase.geometry import find_mic

    symbols = np.asarray(defect.get_chemical_symbols())
    bulk_symbols = np.asarray(bulk.get_chemical_symbols())
    host = np.isin(symbols, bulk_symbols)
    if not host.any():
        raise ValueError("No common host species for origin alignment")
    reference = defect.get_positions()[np.flatnonzero(host)[0]]
    best_score, best_shift = -1, None
    for site in bulk.get_positions()[bulk_symbols == symbols[np.flatnonzero(host)[0]]]:
        shift = find_mic(reference - site, primitive.get_cell())[0]
        delta = defect.get_positions()[:, None, :] - shift - bulk.get_positions()[None, :, :]
        distances = find_mic(delta.reshape(-1, 3), bulk.get_cell())[1].reshape(delta.shape[:2])
        nearest = np.argmin(distances, axis=1)
        score = np.count_nonzero((distances[np.arange(len(symbols)), nearest] < 0.3)
                                 & (symbols == bulk_symbols[nearest]))
        if score > best_score:
            best_score, best_shift = score, shift
        if score == np.count_nonzero(host):
            break
    if best_score <= 0:
        raise ValueError("Could not align defect sites to the bulk lattice")
    print(f"Origin alignment: {best_shift.tolist()} A, {best_score} matching host atoms", file=sys.stderr)
    return best_shift


def analyse(fc_dir, gs_dir, es_dir, triplet_dir=None, zpl_exp=None, sigma=6.0, gamma=2.0,
            bulk_fc_dir=None, mult=None, dcutoff=None, bcutoff=None):
    """Read inputs and return arrays plus metadata; publishing is a separate operation."""
    if (bulk_fc_dir is None) != (mult is None):
        raise ValueError("bulk-fc-dir and mult must be supplied together")
    if mult is not None and (len(mult) != 3 or any(not isinstance(n, int) or n <= 0 for n in mult)):
        raise ValueError("mult must contain three positive integers")
    for name, value in (("dcutoff", dcutoff), ("bcutoff", bcutoff)):
        if value is not None and (bulk_fc_dir is None or not math.isfinite(value) or value <= 0):
            raise ValueError(f"{name} requires embedding and must be finite and positive (Angstrom)")
    for name, value in (("sigma", sigma), ("gamma", gamma)):
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"{name} must be finite and positive (meV)")
    if zpl_exp is not None and (not math.isfinite(zpl_exp) or zpl_exp <= 0):
        raise ValueError("Experimental ZPL must be finite and positive (eV)")
    paths = {"fc_dir": Path(fc_dir).resolve(), "gs_dir": Path(gs_dir).resolve(),
             "es_dir": Path(es_dir).resolve(),
             "triplet_dir": Path(triplet_dir).resolve() if triplet_dir else None}
    if bulk_fc_dir is not None:
        paths["bulk_fc_dir"] = Path(bulk_fc_dir).resolve()
    for path in paths.values():
        if path is not None and not path.is_dir():
            raise ValueError(f"Input directory does not exist: {path}")
    gs_energy, es_energy = read_toten(paths["gs_dir"]), read_toten(paths["es_dir"])
    zpls = {"computed": es_energy - gs_energy,
            "computed_spin_purified": (2 * es_energy - read_toten(paths["triplet_dir"]) - gs_energy
                                       if triplet_dir else None), "experimental": zpl_exp}
    if any(value is not None and value <= 0 for value in zpls.values()):
        raise ValueError("ZPL energies must be positive for emission")

    with redirect_stdout(sys.stderr):
        from dephonopy.force_constants import ForceConstants
        from dephonopy.hr_factors.fc_displacements import (hr_factors_from_displacements,
                                                          compare_systems_and_embedd_array)
        from dephonopy.hr_factors.io import read_atomic_displacements, gaussian_smoothing
        from dephonopy.hr_factors.luminescence import calc_luminescence

        fc = ForceConstants(str(paths["fc_dir"]))
        # Enforce permutation symmetry and the acoustic sum rule before diagonalization.
        fc.ensure_symmetry()
        atoms = fc.atoms
        symbols = atoms.get_chemical_symbols()
        masses = np.asarray(atoms.get_masses(), dtype=float)
        cell = np.asarray(atoms.get_cell(), dtype=float)
        displacement, gs, es = read_atomic_displacements(
            str(paths["gs_dir"] / "CONTCAR"), str(paths["es_dir"] / "CONTCAR"), verbose=False)
        for geometry in (gs, es):
            if geometry.get_chemical_symbols() != symbols:
                raise ValueError("FC, ground and excited structures must have the same ordered species")
            if not np.allclose(geometry.get_cell(), cell, rtol=0, atol=1e-5):
                raise ValueError("FC, ground and excited structures must have the same cell")
            if not np.allclose(geometry.get_masses(), masses, rtol=0, atol=1e-5):
                raise ValueError("FC, ground and excited structures must have the same masses")
        if not np.isfinite(masses).all() or np.any(masses <= 0):
            raise ValueError("Atomic masses must be finite and positive")
        # dephonopy supplies minimum-image displacement in Bohr; remove mass-weighted drift.
        displacement = np.asarray(displacement, dtype=float)
        displacement -= np.average(displacement, axis=0, weights=masses)
        embedding, info = None, None
        if bulk_fc_dir is not None:
            from dephonopy.embedding.defect_fc import construct_and_embed_defect_serial

            bulk_fc = ForceConstants(str(paths["bulk_fc_dir"]))
            bulk_fc.ensure_symmetry()
            primitive = bulk_fc.primitive_cell
            shift = origin_shift(fc.atoms, bulk_fc.atoms, primitive)
            for geometry in (fc.atoms, gs):
                geometry.set_positions(geometry.get_positions() - shift)
                geometry.wrap()
            fc, info = construct_and_embed_defect_serial(
                bulk_fc, mult, fc, unit_cell=primitive, dcutoff=dcutoff, bcutoff=bcutoff)
            fc.ensure_symmetry()
            embedding = {"bulk_fc_dir": str(paths["bulk_fc_dir"]), "mult": list(mult),
                         "origin_shift_A": shift.tolist(), "dcutoff_A": float(info["dcutoff"]),
                         "bcutoff_A": float(bcutoff if bcutoff is not None else bulk_fc.max_cutoff),
                         "defect_sites": len(info["positions"])}
        from dephonopy.matrix_storage import serial_matrix

        # Override only the dense serial solver, leaving numpy and all other users untouched.
        serial_np = serial_matrix.np
        serial_matrix.np = SimpleNamespace(**(vars(serial_np) | {
            "linalg": SimpleNamespace(**(vars(serial_np.linalg) | {
                "eigh": lambda a: eigh(a, driver="evd")}))}))
        try:
            modes = fc.calculate_modes(defect_info=info)
        finally:
            serial_matrix.np = serial_np
        modes.calculate_irreps()
        modes.calculate_iprs()
        if embedding is not None:
            reference = hr_factors_from_displacements(modes, displacement, small_atoms=gs)
            displacement, _ = compare_systems_and_embedd_array(
                modes, displacement, small_atoms=gs, cutoff=None, defect_sites=None,
                bulk_atoms=None, nm="displacements")
        else:
            reference = hr_factors_from_displacements(modes, displacement)
        atoms = modes.atoms
        symbols = atoms.get_chemical_symbols()
        masses = np.asarray(atoms.get_masses(), dtype=float)
        cell = np.asarray(atoms.get_cell(), dtype=float)
        if not np.isfinite(masses).all() or np.any(masses <= 0):
            raise ValueError("Atomic masses must be finite and positive")
        displacement *= physical_constants["Bohr radius"][0] * 1e10
        frequencies = np.asarray(modes.get_frequencies("mev"), dtype=float)
        n_atoms, n_modes = len(masses), len(frequencies)
        if n_modes != 3 * n_atoms:
            raise ValueError("A complete 3N Gamma spectrum is required")
        eigenvectors = np.asarray(modes.displacements, dtype=float).reshape(n_modes, n_atoms, 3)
        eigenvectors = eigenvectors * np.sqrt(masses)[None, :, None]
        norms = np.linalg.norm(eigenvectors, axis=(1, 2))
        if np.any(norms == 0) or not np.isfinite(norms).all():
            raise ValueError("Invalid phonon eigenvector norms")
        eigenvectors /= norms[:, None, None]
        translations, q, s_k = mode_factors(frequencies, eigenvectors, masses, displacement)
        is_translation = np.zeros(n_modes, dtype=bool)
        is_translation[translations] = True
        # The library drops indices 0:3; compare only the common non-translation modes.
        # dephonopy's sqrt(omega) gives NaN for imaginary modes; our signed formula does not.
        common = ~is_translation[3:]
        finite_reference = np.isfinite(reference["hr_factors"])
        if (np.any(common & ~finite_reference & (frequencies[3:] >= 0))
                or not np.allclose(s_k[3:][common & finite_reference],
                                   reference["hr_factors"][common & finite_reference], rtol=1e-3, atol=1e-6)):
            raise ValueError("Huang-Rhys unit/normalization check against dephonopy failed")
        n_eff = np.asarray(modes.iprs, dtype=float)
        finite_s = np.isfinite(s_k)
        if (not all(np.isfinite(a).all() for a in (frequencies, displacement, n_eff))
                or np.any(~finite_s & (frequencies >= 0))
                or np.any(n_eff <= 0)):
            raise ValueError("Non-finite phonon data or invalid participation counts")
        s_total = float(s_k[finite_s].sum())
        imaginary = np.flatnonzero((frequencies < -0.5) & ~is_translation)
        # Even FFT length avoids dephonopy's odd-grid phase error; pad multiphonon tails.
        extent = max(2000.0, float(np.max(np.abs(frequencies))) * (2 * max(s_total, 1) + 12),
                     100 * gamma)
        step = min(0.2, sigma / 20, gamma / 10)
        n_grid = 2 ** math.ceil(math.log2(extent / step + 1))
        # shortcut: dense serial spectra, reject excessive grids until a streaming API is needed.
        if n_grid > 2**20:
            raise ValueError("Requested broadening requires more than 1048576 spectrum samples")
        spectral_modes = ~is_translation & finite_s
        energy, density = gaussian_smoothing(frequencies[spectral_modes], s_k[spectral_modes],
                                             sig=sigma, n=n_grid, xlim=(0, extent))
        integral = float(np.trapezoid(density, energy))
        if s_total != 0:
            if integral <= 0:
                raise ValueError("Cannot normalize the spectral function")
            density *= s_total / integral
        spectra = []
        for zpl in zpls.values():
            if zpl is None:
                spectra.append(None)
            else:
                photon_energy, intensity = calc_luminescence(energy, density, zpl=zpl * 1000,
                                                             gamma=gamma, absorption=False)
                spectra.append((photon_energy, intensity))
        photon_grid = np.unique(np.concatenate([spectrum[0] for spectrum in spectra if spectrum is not None]))
        photon_grid = photon_grid[photon_grid > 0]
        pl = [photon_grid]
        for spectrum in spectra:
            if spectrum is None:
                pl.append(np.full(photon_grid.shape, np.nan))
            else:
                intensity = np.interp(photon_grid, *spectrum, left=0, right=0)
                if not np.isfinite(intensity).all() or intensity.max() <= 0:
                    raise ValueError("Invalid luminescence spectrum")
                pl.append(intensity / intensity.max())

    units = {"index": "dimensionless, zero-based", "freq_meV": "meV (signed hbar omega)",
             "irrep": "point-group label", "n_eff": "atoms (dephonopy physical-displacement IPR)",
             "beta": "dimensionless, n_atoms / n_eff", "S_k": "dimensionless",
             "q_k_amu05_A": "amu^0.5 Angstrom", "is_translation": "boolean (0 or 1)",
             "energy_meV": "meV", "S_E_per_meV": "1/meV", "photon_energy_eV": "eV",
             "intensity_computed": "relative, max 1", "intensity_spin_purified": "relative, max 1",
             "intensity_experimental": "relative, max 1", "eigenvectors": "dimensionless, mass-weighted",
             "participation": "dimensionless, per-atom squared eigenvector norm",
             "positions_A": "Angstrom", "cell_A": "Angstrom", "masses_amu": "amu",
             "s_total": "dimensionless", "debye_waller": "dimensionless", "zpl_eV": "eV",
             "max_displacement_A": "Angstrom", "delta_Q_amu05_A": "amu^0.5 Angstrom",
             "sigma_meV": "meV, Gaussian standard deviation", "gamma_meV": "meV, Lorentzian width"}
    summary = {"schema": "ds.phonon_vibronic.v1", "n_atoms": n_atoms, "n_modes": n_modes,
               "embedding": embedding,
               "translations": {"indices": translations.tolist(), "freq_meV": frequencies[translations].tolist()},
               "imaginary_modes": [{"index": int(i), "freq_meV": float(frequencies[i])} for i in imaginary],
               "stable": len(imaginary) == 0, "s_total": s_total, "debye_waller": math.exp(-s_total),
               "zpl_eV": zpls, "max_displacement_A": float(np.linalg.norm(displacement, axis=1).max()),
               "delta_Q_amu05_A": float(np.sqrt(np.sum(masses[:, None] * displacement**2))),
               "sigma_meV": sigma, "gamma_meV": gamma,
               "input_paths": {key: str(path) if path else None for key, path in paths.items()},
               "units": units, "force_constant_correction": "dephonopy.ensure_symmetry (permutation + acoustic sum rule)",
               "approximation": "harmonic, equal ground/excited curvatures, displaced oscillators, T=0"}
    return {"summary": summary, "structure": {"symbols": symbols, "positions_A": atoms.get_positions().tolist(),
            "cell_A": cell.tolist(), "masses_amu": masses.tolist()}, "frequencies": frequencies,
            "irreps": modes.irreps, "n_eff": n_eff, "s_k": s_k, "q": q,
            "is_translation": is_translation, "eigenvectors": eigenvectors,
            "spectral_function": np.column_stack((energy, density)), "pl": np.column_stack(pl)}


def write_result(result, directory):
    from ase import Atoms
    from ase.io import write

    directory = Path(directory)
    for name in ("summary", "structure"):
        (directory / f"{name}.json").write_text(json.dumps(result[name], indent=2, allow_nan=False) + "\n",
                                              encoding="utf-8")
    structure = result["structure"]
    write(directory / "POSCAR", Atoms(symbols=structure["symbols"], positions=structure["positions_A"],
                                     cell=structure["cell_A"], pbc=True), format="vasp", vasp5=True, sort=False)
    vectors = result["eigenvectors"]
    np.save(directory / "eigenvectors.npy", vectors.astype(np.float32))
    np.save(directory / "participation.npy", np.sum(vectors**2, axis=2).astype(np.float32))
    with (directory / "modes.tsv").open("w", encoding="utf-8") as handle:
        handle.write("index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation\n")
        for i, freq in enumerate(result["frequencies"]):
            row = (i, freq, result["irreps"][i], result["n_eff"][i],
                   result["summary"]["n_atoms"] / result["n_eff"][i], result["s_k"][i],
                   result["q"][i], int(result["is_translation"][i]))
            handle.write("\t".join(str(value) for value in row) + "\n")
    for name, header in (("spectral_function", "energy_meV\tS_E_per_meV"),
                         ("pl", "photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental")):
        np.savetxt(directory / f"{name}.tsv", result[name], delimiter="\t", header=header,
                   comments="", fmt="%.12g")


def publish(result, out_dir, writer=write_result):
    """Stage a complete sibling directory and retain the previous result on failure."""
    out_dir = Path(out_dir).absolute()
    if out_dir.is_symlink() or (out_dir.exists() and not out_dir.is_dir()):
        raise ValueError("Output must be a directory, not a file or symlink")
    out_dir = out_dir.resolve()
    for value in result["summary"]["input_paths"].values():
        if value is not None:
            path = Path(value)
            if out_dir == path or out_dir in path.parents or path in out_dir.parents:
                raise ValueError("Output directory must not overlap an input directory")
    out_dir.parent.mkdir(parents=True, exist_ok=True)
    # mkdir inherits the parent's ACL; Python 3.13 mkdtemp's Windows ACL blocks sandbox access.
    staging = out_dir.parent / f".{out_dir.name}-{uuid4().hex}"
    staging.mkdir()
    backup = staging / "previous"
    published = False
    try:
        new = staging / "new"
        new.mkdir()
        writer(result, new)
        if out_dir.exists():
            out_dir.replace(backup)
        try:
            new.replace(out_dir)
        except BaseException:
            if backup.exists():
                try:
                    backup.replace(out_dir)
                except OSError as exc:
                    raise RuntimeError(f"Could not restore previous output; retained at {backup}") from exc
            raise
        published = True
    finally:
        if published or not backup.exists():
            try:
                shutil.rmtree(staging)
            except OSError as exc:
                print(f"Could not remove staging directory {staging}: {exc}", file=sys.stderr)
    return out_dir


def main():
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise ValueError(message)

    parser = Parser(description=__doc__)
    for name in ("fc-dir", "gs-dir", "es-dir", "out-dir"):
        parser.add_argument(f"--{name}", required=True, type=Path)
    parser.add_argument("--triplet-dir", type=Path)
    parser.add_argument("--zpl-exp", type=float)
    parser.add_argument("--bulk-fc-dir", type=Path)
    parser.add_argument("--mult", type=parse_mult)
    parser.add_argument("--dcutoff", type=float)
    parser.add_argument("--bcutoff", type=float)
    parser.add_argument("--sigma", type=float, default=6.0, help="Gaussian standard deviation (meV)")
    parser.add_argument("--gamma", type=float, default=2.0, help="Lorentzian broadening (meV)")
    try:
        args = vars(parser.parse_args())
        out_dir = args.pop("out_dir")
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
