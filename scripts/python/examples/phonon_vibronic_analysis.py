"""Gamma phonons and T=0 displaced-oscillator PL; no input files are written."""
from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import json
import math
import os
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
from zpl_formula import evaluate, used_variables


RESULT_UNITS = {"index": "dimensionless, zero-based", "freq_meV": "meV (signed hbar omega)",
         "irrep": "point-group label", "n_eff": "atoms (dephonopy physical-displacement IPR)",
         "beta": "dimensionless, n_atoms / n_eff", "S_k": "dimensionless",
         "q_k_amu05_A": "amu^0.5 Angstrom", "is_translation": "boolean (0 or 1)",
         "energy_meV": "meV", "S_E_per_meV": "1/meV", "photon_energy_eV": "eV",
         "intensity_computed": "relative, max 1", "intensity_spin_purified": "relative, max 1",
         "intensity_experimental": "relative, max 1", "intensity_formula": "relative, max 1",
         "eigenvectors": "dimensionless, mass-weighted",
         "participation": "dimensionless, per-atom squared eigenvector norm",
         "positions_A": "Angstrom", "cell_A": "Angstrom", "masses_amu": "amu",
         "s_total": "dimensionless", "debye_waller": "dimensionless", "zpl_eV": "eV",
         "max_displacement_A": "Angstrom", "delta_Q_amu05_A": "amu^0.5 Angstrom",
         "sigma_meV": "meV, Gaussian standard deviation", "sigma_max_meV": "meV, maximum Gaussian standard deviation",
         "gamma_meV": "meV, Lorentzian width"}


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


def build_spectra(frequencies, s_k, is_translation, s_total, zpls, sigma, gamma, sigma_max=None):
    from dephonopy.hr_factors.io import gaussian_smoothing
    from dephonopy.hr_factors.luminescence import calc_luminescence

    finite_s = np.isfinite(s_k)
    # Even FFT length avoids dephonopy's odd-grid phase error; pad multiphonon tails.
    extent = max(2000.0, float(np.max(np.abs(frequencies))) * (2 * max(s_total, 1) + 12),
                 100 * gamma, 12 * max(sigma, sigma_max or sigma))
    step = min(0.2, sigma / 20, gamma / 10)
    n_grid = 2 ** math.ceil(math.log2(extent / step + 1))
    # shortcut: dense serial spectra, reject excessive grids until a streaming API is needed.
    if n_grid > 2**20:
        raise ValueError("Requested broadening requires more than 1048576 spectrum samples")
    spectral_modes = ~is_translation & finite_s
    sticks = frequencies[spectral_modes]
    # dephonopy uses sig + slope*x: shift its origin to give the requested endpoint widths.
    origin = float(sticks.min()) if sigma_max is not None and len(sticks) else 0.0
    variable = sigma_max if len(sticks) > 1 and np.ptp(sticks) > 0 else None
    energy, density = gaussian_smoothing(sticks - origin, s_k[spectral_modes],
                                         sig=sigma, sig_max=variable, n=n_grid,
                                         xlim=(-origin, extent - origin))
    energy += origin
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
    available = [spectrum[0] for spectrum in spectra if spectrum is not None]
    photon_grid = np.unique(np.concatenate(available)) if available else np.array([], dtype=float)
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

    return np.column_stack((energy, density)), np.column_stack(pl)


def analyse(fc_dir, gs_dir, es_dir, triplet_dir=None, zpl_exp=None, sigma=6.0, gamma=2.0,
            bulk_fc_dir=None, mult=None, dcutoff=None, bcutoff=None, zpl_formula=None,
            gs_at_es_dir=None, es_at_gs_dir=None, label=None, hr_method="displacements",
            hr_cutoff=None, sigma_max=None):
    """Read inputs and return arrays plus metadata; publishing is a separate operation."""
    if hr_method not in ("displacements", "approximate_forces", "forces"):
        raise ValueError("hr-method must be displacements, approximate_forces or forces")
    if hr_method == "approximate_forces" and bulk_fc_dir is None:
        raise ValueError("approximate_forces requires --bulk-fc-dir")
    if hr_method == "forces" and gs_at_es_dir is None:
        raise ValueError("forces requires --gs-at-es-dir (GS forces at the ES geometry)")
    if ((mult is not None and bulk_fc_dir is None)
            or (bulk_fc_dir is not None and mult is None and hr_method != "approximate_forces")):
        raise ValueError("bulk-fc-dir and mult must be supplied together except for approximate_forces")
    if mult is not None and (len(mult) != 3 or any(not isinstance(n, int) or n <= 0 for n in mult)):
        raise ValueError("mult must contain three positive integers")
    for name, value in (("dcutoff", dcutoff), ("bcutoff", bcutoff)):
        if value is not None and (mult is None or not math.isfinite(value) or value <= 0):
            raise ValueError(f"{name} requires embedding and must be finite and positive (Angstrom)")
    for name, value in (("sigma", sigma), ("gamma", gamma)):
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"{name} must be finite and positive (meV)")
    if hr_cutoff is not None and (not math.isfinite(hr_cutoff) or hr_cutoff <= 0):
        raise ValueError("hr-cutoff must be finite and positive (Angstrom)")
    if sigma_max is not None and (not math.isfinite(sigma_max) or sigma_max < sigma):
        raise ValueError("sigma-max must be finite and at least sigma (meV)")
    if zpl_exp is not None and (not math.isfinite(zpl_exp) or zpl_exp <= 0):
        raise ValueError("Experimental ZPL must be finite and positive (eV)")
    paths = {"fc_dir": Path(fc_dir).resolve(), "gs_dir": Path(gs_dir).resolve(),
             "es_dir": Path(es_dir).resolve(),
             "triplet_dir": Path(triplet_dir).resolve() if triplet_dir else None}
    used_names = used_variables(zpl_formula) if zpl_formula is not None else []
    for key, directory in (("gs_at_es_dir", gs_at_es_dir), ("es_at_gs_dir", es_at_gs_dir)):
        paths[key] = Path(directory).resolve() if directory is not None else None
    if bulk_fc_dir is not None:
        paths["bulk_fc_dir"] = Path(bulk_fc_dir).resolve()
    for path in paths.values():
        if path is not None and not path.is_dir():
            raise ValueError(f"Input directory does not exist: {path}")
    gs_energy, es_energy = read_toten(paths["gs_dir"]), read_toten(paths["es_dir"])
    triplet_energy = read_toten(paths["triplet_dir"]) if triplet_dir else None
    zpls = {"computed": es_energy - gs_energy,
            "computed_spin_purified": (2 * es_energy - triplet_energy - gs_energy
                                       if triplet_dir else None), "experimental": zpl_exp, "formula": None}
    formula_info = None
    if zpl_formula is not None:
        energies = {"E_gs": gs_energy, "E_es": es_energy, "E_T": triplet_energy,
                    **{name: read_toten(paths[key]) if name in used_names and paths[key] else None for name, key in
                       (("E_gs@es", "gs_at_es_dir"), ("E_es@gs", "es_at_gs_dir"))}}
        zpls["formula"] = evaluate(zpl_formula, energies)
        formula_info = {"text": zpl_formula, "variables_eV": {name: energies[name] for name in used_names},
                        "value_eV": zpls["formula"]}
    if any(value is not None and value <= 0 for value in zpls.values()):
        raise ValueError("ZPL energies must be positive for emission")

    with redirect_stdout(sys.stderr):
        from dephonopy.force_constants import ForceConstants
        from dephonopy.hr_factors.fc_displacements import (hr_factors_from_displacements,
                                                          hr_factors_from_forces,
                                                          compare_systems_and_embedd_array)
        from dephonopy.hr_factors.io import read_atomic_displacements, read_force_differences

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
        shift = np.zeros(3)
        if bulk_fc_dir is not None:
            bulk_fc = ForceConstants(str(paths["bulk_fc_dir"]))
            bulk_fc.ensure_symmetry()
            primitive = bulk_fc.primitive_cell
            shift = origin_shift(fc.atoms, bulk_fc.atoms, primitive)
            if hr_method == "approximate_forces" and (len(bulk_fc.atoms) != len(gs)
                    or not np.allclose(bulk_fc.atoms.get_cell(), cell, rtol=0, atol=1e-5)):
                raise ValueError("approximate_forces requires bulk FC in the same cell as GS/ES")
        if mult is not None:
            from dephonopy.embedding.defect_fc import construct_and_embed_defect_serial

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
        if hr_method == "approximate_forces":
            from dephonopy.hr_factors.harmonic_forces import calculate_force_differences_from_displacements

            # calculate_modes mass-weights the FC in place: embedding must finish first.
            bulk_modes = bulk_fc.calculate_modes()
            bulk_positions = bulk_modes.atoms.get_positions().copy()
            try:
                bulk_modes.atoms.set_positions(bulk_positions + shift)
                bulk_modes.atoms.wrap()
                forces, force_atoms, _ = calculate_force_differences_from_displacements(
                    str(paths["es_dir"] / "CONTCAR"), str(paths["gs_dir"] / "CONTCAR"), bulk_modes)
            finally:
                bulk_modes.atoms.set_positions(bulk_positions)
        elif hr_method == "forces":
            # Emission: initial ES, final GS PES at the same ES geometry; returns F_ES - F_GS@ES.
            forces, force_atoms, final_atoms = read_force_differences(
                str(paths["es_dir"]), str(paths["gs_at_es_dir"]), check_equal_positions=True)
            if (final_atoms.get_chemical_symbols() != symbols
                    or not np.allclose(final_atoms.get_masses(), masses, rtol=0, atol=1e-5)):
                raise ValueError("GS at ES must have the same ordered species and masses as ES")
        if hr_method != "displacements" and (np.shape(forces) != (len(gs), 3) or not np.isfinite(forces).all()):
            raise ValueError("Expected finite force differences for every GS/ES atom")
        cutoff = hr_cutoff
        sites = None
        if embedding is not None or cutoff is not None:
            from dephonopy.embedding.atoms import check_defect_cutoff_radius

            # Without embedding use every atom tied for the largest GS/ES displacement.
            lengths = np.linalg.norm(displacement, axis=1)
            sites = info["positions"] if info is not None else gs.get_positions()[
                np.isclose(lengths, lengths.max(), rtol=1e-5, atol=1e-10)]
            try:
                cutoff = check_defect_cutoff_radius(gs, sites, cutoff, verbose=False)
            except RuntimeError:
                cutoff = check_defect_cutoff_radius(gs, sites, None, verbose=False)
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
        if hr_method == "displacements":
            reference = hr_factors_from_displacements(modes, displacement.copy(),
                small_atoms=gs if cutoff is not None else None, cutoff=cutoff, defect_sites=sites)
        else:
            if mult is not None:
                force_atoms.set_positions(force_atoms.get_positions() - shift)
                force_atoms.wrap()
            reference = hr_factors_from_forces(modes, forces, small_atoms=force_atoms if cutoff is not None else None,
                                               cutoff=cutoff, defect_sites=sites)
        if cutoff is not None:
            displacement, _ = compare_systems_and_embedd_array(
                modes, displacement, small_atoms=gs, cutoff=cutoff, defect_sites=sites,
                bulk_atoms=None, nm="displacements")
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
        if hr_method != "displacements":
            # Force methods use dephonopy's mode projections and S_k, never displacement-derived S_k.
            s_k = np.r_[np.zeros(3), reference["hr_factors"]]
            q = np.r_[np.zeros(3), reference["fc_displ"]] * np.sqrt(
                physical_constants["electron mass"][0] / atomic_mass) * physical_constants["Bohr radius"][0] * 1e10
            s_k[translations] = 0.0
            q[translations] = 0.0
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
        spectral_function, pl = build_spectra(frequencies, s_k, is_translation, s_total, zpls, sigma, gamma, sigma_max)

    summary = {"schema": "ds.phonon_vibronic.v1", "n_atoms": n_atoms, "n_modes": n_modes,
               "source": "local", "has_eigenvectors": True, "hr_method": hr_method,
               "hr_cutoff_A": float(cutoff) if cutoff is not None else None, "supercell": list(mult) if mult is not None else None,
               "label": label if label is not None else paths["es_dir"].name + (
                   " (emb " + "x".join(map(str, mult)) + ")" if embedding is not None else ""),
               "zpl_formula": formula_info,
               "embedding": embedding,
               "translations": {"indices": translations.tolist(), "freq_meV": frequencies[translations].tolist()},
               "imaginary_modes": [{"index": int(i), "freq_meV": float(frequencies[i])} for i in imaginary],
               "stable": len(imaginary) == 0, "s_total": s_total, "debye_waller": math.exp(-s_total),
               "zpl_eV": zpls, "max_displacement_A": float(np.linalg.norm(displacement, axis=1).max()),
               "delta_Q_amu05_A": float(np.sqrt(np.sum(masses[:, None] * displacement**2))),
               "sigma_meV": sigma, "sigma_max_meV": sigma_max, "gamma_meV": gamma,
               "input_paths": {key: str(path) if path else None for key, path in paths.items()},
               "units": RESULT_UNITS, "force_constant_correction": "dephonopy.ensure_symmetry (permutation + acoustic sum rule)",
               "approximation": "harmonic, equal ground/excited curvatures, displaced oscillators, T=0"}
    return {"summary": summary, "structure": {"symbols": symbols, "positions_A": atoms.get_positions().tolist(),
            "cell_A": cell.tolist(), "masses_amu": masses.tolist()}, "frequencies": frequencies,
            "irreps": modes.irreps, "n_eff": n_eff, "s_k": s_k, "q": q,
            "is_translation": is_translation, "eigenvectors": eigenvectors,
            "spectral_function": spectral_function, "pl": pl}


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
    vectors = result.get("eigenvectors")
    if vectors is not None:
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
                         ("pl", "photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\tintensity_formula")):
        np.savetxt(directory / f"{name}.tsv", result[name], delimiter="\t", header=header,
                   comments="", fmt="%.12g")


def publish(result, out_dir, writer=write_result):
    """Stage a complete sibling directory and retain the previous result on failure."""
    out_dir = Path(out_dir).absolute()
    if out_dir.is_symlink() or (out_dir.exists() and not out_dir.is_dir()):
        raise ValueError("Output must be a directory, not a file or symlink")
    out_dir = out_dir.resolve()
    relative = {}
    for key, value in result["summary"]["input_paths"].items():
        relative[key] = None
        if value is not None:
            path = Path(value).resolve()
            if out_dir == path or out_dir in path.parents:
                raise ValueError("Output directory must not overlap an input directory")
            try:
                relative[key] = os.path.relpath(path, out_dir).replace("\\", "/")
            except ValueError:  # Windows paths on different drives cannot be relative.
                relative[key] = path.as_posix()
    result["summary"]["inputs_relative"] = relative
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
    parser.add_argument("--zpl-formula")
    parser.add_argument("--gs-at-es-dir", type=Path)
    parser.add_argument("--es-at-gs-dir", type=Path)
    parser.add_argument("--label")
    parser.add_argument("--bulk-fc-dir", type=Path)
    parser.add_argument("--mult", type=parse_mult)
    parser.add_argument("--dcutoff", type=float)
    parser.add_argument("--bcutoff", type=float)
    parser.add_argument("--sigma", type=float, default=6.0, help="Gaussian standard deviation (meV)")
    parser.add_argument("--sigma-max", type=float)
    parser.add_argument("--hr-method", choices=("displacements", "approximate_forces", "forces"), default="displacements")
    parser.add_argument("--hr-cutoff", type=float)
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
