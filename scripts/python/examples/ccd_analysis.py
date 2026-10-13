"""Converged CCD single points -> 1D harmonic, Numerov and phonon-effective PL.

Point mode (no CCD path, task 105b): --gs-dir/--es-dir relaxed minima, optional --triplet-dir
(ZPL = 2 E_es - E_T - E_gs), curvature from --gs-at-es-dir / --es-at-gs-dir single points
(E_rel = omega^2 dQ^2 / 2; one of them -> equal curvatures) or from --phonon-result.
"""
from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import csv
import json
import math
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True

import numpy as np
from scipy.interpolate import CubicSpline
from puntukas.units import convert
from puntukas.vibronic import (boltzmann_weights, franck_condon_model, harmonic_model, huang_rhys,
                              luminescence_band, solve_bound_states, turning_points)

from ccd_inputs import dirname_for
from phonon_vibronic_analysis import publish
from zpl_formula import evaluate, used_variables

Q_AU = convert(1.0, "angstrom", "bohr") * math.sqrt(convert(1.0, "amu", "me"))
FINAL_TOTEN = re.compile(r"free  energy   TOTEN\s*=\s*([-+\d.EeDd]+)")


def read_optional(path):
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def final_toten(directory, required=False):
    energy = None
    if (directory / "OUTCAR").is_file():
        with (directory / "OUTCAR").open(encoding="utf-8", errors="replace") as handle:
            for line in handle:
                match = FINAL_TOTEN.search(line)
                if match:
                    energy = float(match[1].replace("D", "E").replace("d", "e"))
    if energy is not None and not math.isfinite(energy):
        raise ValueError(f"Non-finite final TOTEN in {directory}")
    if energy is None and required:
        raise ValueError(f"No final TOTEN in {directory / 'OUTCAR'}")
    return energy


def delta_q_between(gs_dir, es_dir):
    """Mass-weighted minimum-image distance between two CONTCARs, centre-of-mass drift removed."""
    from ase.io import read

    gs, es = (read(directory / "CONTCAR", format="vasp") for directory in (gs_dir, es_dir))
    if gs.get_chemical_symbols() != es.get_chemical_symbols() or not np.allclose(
            gs.cell.array, es.cell.array, rtol=0, atol=1e-5):
        raise ValueError("Ground and excited CONTCARs must have the same ordered species and cell")
    fractional = es.get_scaled_positions(wrap=False) - gs.get_scaled_positions(wrap=False)
    displacement = (fractional - np.round(fractional)) @ gs.cell.array
    masses = gs.get_masses()
    displacement -= np.average(displacement, axis=0, weights=masses)
    return float(np.sqrt(masses @ (displacement**2).sum(axis=1)))


def point_wells(paths):
    """Two relaxed minima (+ triplet, + single points at the other minimum) -> 1D harmonic wells."""
    energy = {key: final_toten(paths[key], required=True) for key in ("gs_dir", "es_dir")}
    dq = delta_q_between(paths["gs_dir"], paths["es_dir"])
    if dq <= 0:
        raise ValueError("Ground and excited geometries are identical")
    zpls = {"from_ccd": energy["es_dir"] - energy["gs_dir"], "spin_purified": (
        2 * energy["es_dir"] - final_toten(paths["triplet_dir"], required=True) - energy["gs_dir"]
        if paths["triplet_dir"] else None)}
    omegas, sources = {}, {}
    for branch, key, minimum in (("gs", "gs_at_es_dir", "gs_dir"), ("es", "es_at_gs_dir", "es_dir")):
        if paths[key]:
            relaxation = final_toten(paths[key], required=True) - energy[minimum]
            if relaxation <= 0:
                raise ValueError(f"{branch}: the single point at the other geometry must lie above the minimum")
            # E_rel = omega^2 dQ^2 / 2 in atomic units (mass-weighted, m = 1).
            omegas[branch], sources[branch] = math.sqrt(2 * convert(relaxation, "eV", "Ha")) / (dq * Q_AU), key
    if len(omegas) == 1:
        (known, omega), = omegas.items()
        other = "es" if known == "gs" else "gs"
        omegas[other], sources[other] = omega, f"equal curvature assumed ({known})"
    harmonic = None
    if omegas:
        harmonic = {}
        for branch, omega in omegas.items():
            s, mev = huang_rhys(omega, dq * Q_AU), convert(omega, "Ha", "meV")
            harmonic[branch] = dict(hbar_omega_meV=mev, S=s, relaxation_eV=s * mev / 1000,
                                    curvature_from=sources[branch])
    return dq, zpls, harmonic, omegas


def read_points(directory):
    points = []
    with (directory / "ccd_path.csv").open(encoding="ascii", newline="") as handle:
        for row in csv.DictReader(handle):
            branch, t, q = row["branch"], float(row["t"]), float(row["Q_amu05_A"])
            if branch not in ("gs", "es") or not all(map(math.isfinite, (t, q))):
                raise ValueError("Invalid CCD branch or coordinate")
            point_dir = directory / branch / dirname_for(t)
            incar = re.sub(r"[!#][^\n]*", "", read_optional(point_dir / "INCAR"))
            match = re.search(r"\bNELM\s*=\s*(\d+)\b", incar, re.IGNORECASE)
            nelm = int(match[1]) if match else 60
            if nelm <= 0:
                raise ValueError(f"Invalid NELM in {point_dir}")
            lines = read_optional(point_dir / "OSZICAR").splitlines()
            ends = [i for i, line in enumerate(lines) if re.search(r"\bF\s*=", line)]
            start = ends[-2] + 1 if len(ends) >= 2 else 0
            steps = sum(bool(re.match(r"\s*(DAV|RMM):", line)) for line in lines[start:])
            energy = final_toten(point_dir)
            status = "missing" if energy is None else "unconverged" if steps >= nelm else "ok"
            points.append(dict(branch=branch, t=t, Q_amu05_A=q, energy_eV=energy,
                               status=status, scf_steps=steps, nelm=nelm))
    minima, data = {}, {}
    for branch in ("gs", "es"):
        good = sorted((p for p in points if p["branch"] == branch and p["status"] == "ok"),
                      key=lambda p: p["Q_amu05_A"])
        if len(good) < 3:
            raise ValueError(f"{branch}: fewer than 3 ok CCD points")
        data[branch] = np.array([[p["Q_amu05_A"], p["energy_eV"]] for p in good])
        if np.any(np.diff(data[branch][:, 0]) <= 0):
            raise ValueError(f"{branch}: CCD coordinates must be distinct")
        minima[branch] = data[branch][np.argmin(data[branch][:, 1])].copy()
    for p in points:
        if p["energy_eV"] is not None:
            p["energy_eV"] -= minima["gs"][1]
    for branch in data:
        data[branch][:, 1] -= minima["gs"][1]
    return points, data, minima


def fit_well(data, delta_q):
    q, energy = data.T
    i = int(np.argmin(energy))
    neighbours = [i - 1, i + 1] if 0 < i < len(q) - 1 else list(
        np.argsort(np.abs(q - q[i]))[1:3])
    indices = [i, *neighbours]
    a, _, _ = np.polyfit(q[indices] - q[i], energy[indices] - energy[i], 2)
    if not math.isfinite(a) or a <= 0:
        raise ValueError("Harmonic fit must have positive curvature")
    omega = math.sqrt(convert(2 * a, "eV", "Ha") / Q_AU**2)
    s = huang_rhys(omega, delta_q * Q_AU)
    mev = convert(omega, "Ha", "meV")
    return dict(hbar_omega_meV=mev, S=s, relaxation_eV=s * mev / 1000), omega


def continued_spline(data):
    q, energy = data.T
    spline = CubicSpline(q, energy)

    def potential(x):
        x = np.asarray(x)
        v = spline(x)
        for edge, outside in ((q[0], x < q[0]), (q[-1], x > q[-1])):
            dx = x - edge
            tail = spline(edge) + spline(edge, 1) * dx + 0.5 * max(float(spline(edge, 2)), 1e-6) * dx**2
            v = np.where(outside, tail, v)
        return v

    return potential


def ladder_sizes(omega_es, omega_gs, delta_q, temperature, n_states):
    kt = convert(temperature, "K", "Ha")
    ni = max(3, n_states or 0, math.ceil(20 * kt / omega_es) + 1)
    s = max(huang_rhys(w, delta_q * Q_AU) for w in (omega_es, omega_gs))
    nf = max(12, n_states or 0, math.ceil(s + 8 * math.sqrt(s + 1) + 8
                                      + ni * omega_es / omega_gs))
    return ni, nf


def harmonic_pl_model(omega_es, omega_gs, delta_q, temperature):
    ni = math.ceil(20 * convert(temperature, "K", "Ha") / omega_es) + 1
    # Stop once the sum closes: unused high levels lose digits in the analytic formula.
    for nf in range(12, 121, 12):
        model = harmonic_model(omega_es, omega_gs, delta_q * Q_AU, n_initial=ni, n_final=nf)
        completeness = model.completeness()
        if not np.isfinite(completeness).all() or np.any(completeness > 1 + 1e-6):
            raise ValueError("Harmonic overlaps lost accuracy; use a shorter thermal ladder")
        populations = boltzmann_weights(model.initial_energies, temperature)
        # A 1% omitted tail can exceed 1% of the peak in the normalised PL band.
        if completeness[0] > 0.9999 and populations @ completeness > 0.9999:
            return model
    raise ValueError("Harmonic Franck-Condon sum did not close")


def numerov_model(data, omegas, delta_q, temperature, n_states):
    potentials = {branch: continued_spline(values) for branch, values in data.items()}
    ni, nf = ladder_sizes(omegas["es"], omegas["gs"], delta_q, temperature, n_states)
    margin = math.sqrt(2 * (max(ni, nf) + 20) / min(omegas.values())) / Q_AU
    kt = convert(temperature, "K", "Ha")
    for _ in range(10):
        lo = min(values[0, 0] for values in data.values()) - margin
        hi = max(values[-1, 0] for values in data.values()) + margin
        samples = max(2001, math.ceil((hi - lo) * Q_AU * math.sqrt(
            2 * max(ni, nf) * max(omegas.values())) / 0.08) + 1)
        # shortcut: bounded in-memory grid, raise the ceiling when larger ladders are needed.
        if samples > 40001:
            raise ValueError("Requested Numerov model needs more than 40001 grid points")
        x = np.linspace(lo * Q_AU, hi * Q_AU, samples)
        curves = {branch: potential(x / Q_AU) for branch, potential in potentials.items()}
        floors = {branch: float(v.min()) for branch, v in curves.items()}
        states = {}
        try:
            for branch, count in (("gs", nf), ("es", ni)):
                v = convert(curves[branch] - floors[branch], "eV", "Ha")
                states[branch] = solve_bound_states(v, x, n_states=count, mass=1.0)
        except ValueError as exc:
            if "fewer than" not in str(exc) and "edge of the grid" not in str(exc):
                raise
            margin *= 1.5
            continue
        for branch, ladder in states.items():
            if not ladder.converged.all():
                raise ValueError(f"{branch}: Numerov levels did not converge")
        contained = True
        for ladder in states.values():
            roots = turning_points(ladder.potential, ladder.energies[-1], x)
            contained &= (len(roots) >= 2 and x[0] < roots[0] < roots[-1] < x[-1]
                          and min(ladder.potential[0], ladder.potential[-1]) > ladder.energies[-1])
        if not contained:
            margin *= 1.5
            continue
        model = franck_condon_model(states["es"], states["gs"])
        if kt > 0 and model.initial_energies[-1] < 20 * kt:
            ni *= 2
            margin *= 1.5
            continue
        if np.min(model.completeness()) <= 0.9999:
            nf = math.ceil(nf * 1.5)
            margin *= 1.5
            continue
        levels = {branch: convert(ladder.energies, "Ha", "meV").tolist()
                  for branch, ladder in states.items()}
        info = dict(gs_levels_meV=levels["gs"], es_levels_meV=levels["es"],
                    fc_0n=model.factors[0].tolist(), fc_sum=float(model.completeness()[0]))
        indices = np.unique(np.linspace(0, len(x) - 1, min(2001, len(x)), dtype=int))
        wave_columns, wave_names = [x[indices] / Q_AU], ["Q_amu05_A"]
        for branch in ("gs", "es"):
            for n, wave in enumerate(states[branch].wavefunctions[:8]):
                wave_columns.append(wave[indices] * math.sqrt(Q_AU))
                wave_names.append(f"{branch}_{n}")
        waves = dict(values=np.column_stack(wave_columns), header="\t".join(wave_names))
        return model, info, np.column_stack((x / Q_AU, curves["gs"], curves["es"])), waves
    raise ValueError("Could not contain the vibrational ladders and close the Franck-Condon sum")


def phonon_mode(directory):
    summary = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
    if summary.get("schema") != "ds.phonon_vibronic.v1":
        raise ValueError("Expected a ds.phonon_vibronic.v1 phonon result")
    with (directory / "modes.tsv").open(encoding="utf-8", newline="") as handle:
        modes = [(float(row["freq_meV"]), float(row["q_k_amu05_A"]))
                 for row in csv.DictReader(handle, delimiter="\t") if not int(row["is_translation"])]
    values = np.asarray(modes)
    if not modes or not np.isfinite(values).all() or np.any(values[:, 0] <= 0):
        raise ValueError("Phonon effective mode needs finite, positive non-translation frequencies")
    dq2 = float(np.sum(values[:, 1]**2))
    if dq2 <= 0:
        raise ValueError("Phonon effective mode needs a nonzero displacement")
    dq = math.sqrt(dq2)
    mev = math.sqrt(float(np.sum((values[:, 0] * values[:, 1])**2)) / dq2)
    omega = convert(mev, "meV", "Ha")
    return dict(hbar_omega_eff_meV=mev, delta_Q_amu05_A=dq,
                S=huang_rhys(omega, dq * Q_AU)), omega, summary["zpl_eV"]


def analyse(ccd_dir=None, phonon_result=None, zpl=None, sigma=6.0, temperature=0.0, n_states=None,
            gs_dir=None, es_dir=None, triplet_dir=None, gs_at_es_dir=None, es_at_gs_dir=None,
            zpl_formula=None, label=None):
    if ccd_dir is None and phonon_result is None and gs_dir is None:
        raise ValueError("Supply --ccd-dir, --gs-dir/--es-dir or --phonon-result")
    if (gs_dir is None) != (es_dir is None):
        raise ValueError("--gs-dir and --es-dir must be supplied together")
    if gs_dir is None and (triplet_dir or gs_at_es_dir or es_at_gs_dir):
        raise ValueError("--triplet-dir, --gs-at-es-dir and --es-at-gs-dir need --gs-dir/--es-dir")
    if gs_dir is not None and ccd_dir is not None:
        raise ValueError("Use either --ccd-dir or --gs-dir/--es-dir, not both")
    if not math.isfinite(sigma) or sigma <= 0:
        raise ValueError("sigma must be finite and positive (meV)")
    if not math.isfinite(temperature) or temperature < 0:
        raise ValueError("temperature must be finite and non-negative (K)")
    if n_states is not None and n_states <= 0:
        raise ValueError("n-states must be positive")
    named = dict(ccd_dir=ccd_dir, phonon_result=phonon_result, gs_dir=gs_dir, es_dir=es_dir,
                 triplet_dir=triplet_dir, gs_at_es_dir=gs_at_es_dir, es_at_gs_dir=es_at_gs_dir)
    paths = {key: Path(value).resolve() if value else None for key, value in named.items()}
    for key, path in paths.items():
        if path is not None and not path.is_dir():
            raise ValueError(f"Input directory does not exist: {path}")
    points, data, harmonic, numerov, phonon = [], None, None, None, None
    curves, from_ccd, spin_purified, dq = np.empty((0, 3)), None, None, None
    models, zpls = {}, {}
    if paths["phonon_result"]:
        phonon, omega, zpls = phonon_mode(paths["phonon_result"])
        dq = phonon["delta_Q_amu05_A"]
        models["phonon_1d"] = harmonic_pl_model(omega, omega, dq, temperature)
    if paths["ccd_dir"]:
        points, data, minima = read_points(paths["ccd_dir"])
        from_ccd = float(minima["es"][1] - minima["gs"][1])
        dq = float(minima["es"][0] - minima["gs"][0])
        for point in points:
            point["Q_amu05_A"] -= minima["gs"][0]
        for values in data.values():
            values[:, 0] -= minima["gs"][0]
        harmonic, omegas = {}, {}
        for branch in ("gs", "es"):
            harmonic[branch], omegas[branch] = fit_well(data[branch], dq)
    if paths["gs_dir"]:
        dq, point_zpls, harmonic, omegas = point_wells(paths)
        from_ccd, spin_purified = point_zpls["from_ccd"], point_zpls["spin_purified"]
        if harmonic is None and "phonon_1d" not in models:
            raise ValueError("No curvature: supply --gs-at-es-dir/--es-at-gs-dir or --phonon-result")
        gs_energy = final_toten(paths["gs_dir"], required=True)
        for branch, t, key in (("gs", 0, "gs_dir"), ("es", 1, "es_dir"),
                               ("gs", 1, "gs_at_es_dir"), ("es", 0, "es_at_gs_dir"),
                               ("triplet", 1, "triplet_dir")):
            if paths[key]:
                points.append(dict(branch=branch, t=t, Q_amu05_A=t * dq,
                                   energy_eV=final_toten(paths[key], required=True) - gs_energy, status="ok"))
    formula_info, formula_zpl = None, None
    if zpl_formula is not None:
        sources = {"E_gs": "gs_dir", "E_es": "es_dir", "E_T": "triplet_dir",
                   "E_gs@es": "gs_at_es_dir", "E_es@gs": "es_at_gs_dir"}
        energies = {name: final_toten(paths[sources[name]], required=True) if paths[sources[name]] else None
                    for name in used_variables(zpl_formula)}
        formula_zpl = evaluate(zpl_formula, energies)
        formula_info = dict(text=zpl_formula, variables_eV=energies, value_eV=formula_zpl)
    candidates = (formula_zpl, zpl, spin_purified, from_ccd, zpls.get("computed_spin_purified"), zpls.get("computed"))
    used = next((value for value in candidates if value is not None), None)
    if used is None or not math.isfinite(used) or used <= 0:
        raise ValueError("ZPL must be finite and positive (eV)")
    synthesized = data is None
    for point in points:
        if point["branch"] == "es" and point["energy_eV"] is not None:
            point["energy_eV"] += used - from_ccd
    if synthesized:
        if harmonic is None:
            omegas = dict(gs=omega, es=omega)
        q = np.linspace(0, dq, 3)
        data = {branch: np.column_stack((q, convert(0.5 * omegas[branch]**2 *
                ((q - centre) * Q_AU)**2, "Ha", "eV") + floor))
                for branch, centre, floor in (("gs", 0, 0), ("es", dq, used))}
        if harmonic is None:
            harmonic = {branch: fit_well(values, dq)[0] for branch, values in data.items()}
    else:
        data["es"][:, 1] += used - from_ccd
    models["numerov"], numerov, curves, waves = numerov_model(data, omegas, dq, temperature, n_states)
    models["harmonic"] = harmonic_pl_model(omegas["es"], omegas["gs"], dq, temperature)
    endpoints = {branch: continued_spline(values)(np.array([0, dq])) for branch, values in data.items()}
    source = "curves"
    for point in points:
        if point["branch"] not in endpoints or point["status"] != "ok":
            continue
        for i, q in enumerate((0, dq)):
            if math.isclose(point["Q_amu05_A"], q, abs_tol=1e-8):
                endpoints[point["branch"]][i] = point["energy_eV"]
                if (point["branch"], i) in (("gs", 1), ("es", 0)):
                    source = "points"
    gs, es = endpoints["gs"], endpoints["es"]
    franck_condon = dict(absorption_eV=float(es[0] - gs[0]), emission_eV=float(es[1] - gs[1]),
                        relaxation_gs_eV=float(gs[1] - gs[0]), relaxation_es_eV=float(es[0] - es[1]),
                        zpl_eV=used, delta_Q_amu05_A=dq, source=source)
    upper = used + 20 * convert(temperature, "K", "eV") + 6 * sigma / 1000
    energies = np.linspace(min(1e-6, upper / 10000), upper, max(4001, math.ceil(upper * 10000 / sigma)))
    pl = [energies]
    for name in ("numerov", "harmonic", "phonon_1d"):
        if name not in models:
            pl.append(np.full(energies.shape, np.nan))
            continue
        _, intensity = luminescence_band(models[name], sigma=convert(sigma, "meV", "Ha"),
                                        zpl=convert(used, "eV", "Ha"), temperature=temperature,
                                        grid=convert(energies, "eV", "Ha"))
        if not np.isfinite(intensity).all() or intensity.max() <= 0:
            raise ValueError(f"Invalid {name} luminescence band")
        pl.append(intensity / intensity.max())
    summary = dict(schema="ds.ccd_analysis.v1", points=points, delta_Q_amu05_A=dq,
                   label=label if label is not None else (paths["es_dir"] or paths["ccd_dir"] or paths["phonon_result"]).name,
                   zpl_formula=formula_info,
                   zpl_eV=dict(from_ccd=from_ccd, spin_purified=spin_purified, used=used, formula=formula_zpl),
                   harmonic=harmonic, numerov=numerov, franck_condon=franck_condon,
                   phonon_1d=phonon, sigma_meV=sigma, temperature_K=temperature,
                   input_paths={key: str(path) if path else None for key, path in paths.items()},
                   units={"Q_amu05_A": "amu^0.5 Angstrom", "delta_Q_amu05_A": "amu^0.5 Angstrom",
                          "energy_eV": "eV, relative to gs minimum; es includes rigid ZPL shift", "zpl_eV": "eV",
                          "franck_condon": "energies in eV; delta_Q_amu05_A in amu^0.5 Angstrom",
                          "wavefunctions": "(amu^0.5 Angstrom)^-0.5; integral |chi|^2 dQ = 1",
                          "levels_meV": "meV, relative to each potential bottom", "hbar_omega_meV": "meV",
                          "sigma_meV": "meV, Gaussian standard deviation", "temperature_K": "K",
                          "S": "dimensionless", "fc_0n": "dimensionless", "intensity": "relative, max 1"},
                   approximation="1D configuration coordinate; local harmonic fits; cubic CCD spline with "
                   "bound quadratic tails; rigid excited-state ZPL shift; phonon effective mode has equal curvatures")
    if synthesized:
        summary["approximation"] += "; synthesized harmonic branches from point curvatures or phonon effective mode"
    return dict(summary=summary, curves=curves, wavefunctions=waves, pl=np.column_stack(pl))


def write_result(result, directory):
    (directory / "summary.json").write_bytes((json.dumps(result["summary"], indent=2, allow_nan=False)
                                               + "\n").encode("ascii"))
    with (directory / "ccd_points.tsv").open("w", encoding="ascii", newline="\n") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        columns = ("branch", "t", "Q_amu05_A", "energy_eV", "status")
        writer.writerow(columns)
        writer.writerows([point[key] for key in columns] for point in result["summary"]["points"])
    with (directory / "levels.tsv").open("w", encoding="ascii", newline="\n") as handle:
        handle.write("branch\tn\tenergy_meV\n")
        if result["summary"]["numerov"]:
            for branch in ("gs", "es"):
                for n, energy in enumerate(result["summary"]["numerov"][f"{branch}_levels_meV"]):
                    handle.write(f"{branch}\t{n}\t{energy:.12g}\n")
    for name, array, header in (("ccd_curves", result["curves"], "Q_amu05_A\tV_gs_eV\tV_es_eV"),
                                 ("wavefunctions", result["wavefunctions"]["values"], result["wavefunctions"]["header"]),
                                 ("pl_1d", result["pl"], "photon_energy_eV\tintensity_numerov\t"
                                  "intensity_harmonic\tintensity_phonon_1d")):
        with (directory / f"{name}.tsv").open("w", encoding="ascii", newline="\n") as handle:
            np.savetxt(handle, array, delimiter="\t", header=header, comments="", fmt="%.12g")


def main():
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise ValueError(message)

    parser = Parser(description=__doc__)
    parser.add_argument("--ccd-dir", type=Path)
    parser.add_argument("--phonon-result", type=Path)
    for name in ("gs-dir", "es-dir", "triplet-dir", "gs-at-es-dir", "es-at-gs-dir"):
        parser.add_argument(f"--{name}", type=Path)
    parser.add_argument("--zpl", type=float)
    parser.add_argument("--zpl-formula")
    parser.add_argument("--label")
    parser.add_argument("--sigma", type=float, default=6.0)
    parser.add_argument("--temperature", type=float, default=0.0)
    parser.add_argument("--n-states", type=int)
    parser.add_argument("--out-dir", type=Path, required=True)
    try:
        args = vars(parser.parse_args())
        out_dir = args.pop("out_dir")
        with redirect_stdout(sys.stderr):
            result = analyse(**args)
            out_dir = publish(result, out_dir, writer=write_result)
        print(json.dumps(dict(ok=True, out_dir=str(out_dir), zpl_eV=result["summary"]["zpl_eV"]["used"])))
        return 0
    except Exception as exc:
        print(json.dumps(dict(ok=False, error=str(exc))))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
