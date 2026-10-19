"""Bulk phonon dispersion along the automatic primitive-cell high-symmetry path."""
from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import json
from pathlib import Path
import sys

# Scientific imports must not write bytecode into the input or vendored trees.
sys.dont_write_bytecode = True

import numpy as np
from phonon_vibronic_analysis import publish


def band_data(phonon):
    """Join connected paths and convert the plotter's eV bands to signed meV."""
    from dephonopy._puntukas_compat import Units

    data = phonon.get_band_structure_dict()
    frequencies = [np.asarray(Units.convert_vasp_energy(f, "meV"), dtype=float)
                   for f in data["frequencies"]]
    distances = data["distances"]
    connections = phonon.band_structure.path_connections
    labels = [label.strip("$").replace(r"\mathrm{", "").replace("}", "")
              .replace(r"\Gamma", "Γ").replace("GAMMA", "Γ")
              for label in phonon.band_structure.labels]
    primitive = phonon.primitive
    n_atoms = len(primitive.symbols)
    n_bands = 3 * n_atoms
    if (not frequencies or len(frequencies) != len(distances)
            or len(frequencies) != len(connections) or len(frequencies) != len(data["qpoints"])):
        raise ValueError("Incomplete phonon band paths")
    ticks, breaks, rows = [], [], []
    piece = 0
    gamma = None
    for i, (freq, dist, qpoints) in enumerate(zip(frequencies, distances, data["qpoints"])):
        dist = np.asarray(dist, dtype=float)
        qpoints = np.asarray(qpoints, dtype=float)
        if (dist.ndim != 1 or len(dist) < 2 or freq.shape != (len(dist), n_bands)
                or qpoints.shape != (len(dist), 3)
                or not all(np.isfinite(a).all() for a in (freq, dist, qpoints))
                or np.any(np.diff(dist) < 0)
                or (i and dist[0] < distances[i - 1][-1])):
            raise ValueError("Invalid phonon bands or path distances")
        if i and not connections[i - 1]:
            label = labels.pop(0) + "/" + labels.pop(0)
            breaks.append(float(dist[0]))
            piece += 1
        else:
            label = labels.pop(0)
        ticks.append({"distance": float(dist[0]), "label": label})
        rows.append(np.column_stack((np.full(len(dist), piece), dist, freq)))
        gamma_indices = np.flatnonzero(np.all(np.isclose(qpoints, 0, rtol=0, atol=1e-8), axis=1))
        if gamma is None and len(gamma_indices):
            gamma = freq[gamma_indices[0]].tolist()
    if len(labels) != 1 or gamma is None:
        raise ValueError("Incomplete high-symmetry labels or missing Gamma point")
    ticks.append({"distance": float(distances[-1][-1]), "label": labels[0]})
    summary = {"schema": "ds.phonon_dispersion.v1", "n_bands": n_bands,
               "n_atoms_primitive": n_atoms, "ticks": ticks, "breaks": breaks,
               "min_freq_meV": float(min(f.min() for f in frequencies)),
               "max_freq_meV": float(max(f.max() for f in frequencies)),
               "gamma_freq_meV": gamma, "primitive_cell_A": primitive.cell.tolist(),
               "primitive_symbols": list(primitive.symbols),
               "units": {"segment": "dimensionless, zero-based continuous piece",
                         "distance": "1/Angstrom (phonopy cumulative path coordinate)",
                         "band_n": "meV (signed hbar omega)", "gamma_freq_meV": "meV",
                         "min_freq_meV": "meV", "max_freq_meV": "meV",
                         "primitive_cell_A": "Angstrom"}}
    summary.update(qpoints_file="qpoints.tsv", q_basis="primitive_reciprocal_reduced")
    return {"summary": summary, "dispersion": np.vstack(rows), "qpoints": np.vstack(data["qpoints"])}


def keep_in_plane(phonon):
    """Rerun the band structure on the segments with q_z = 0 only (2D slab: z is vacuum)."""
    structure = phonon.band_structure
    labels, connections = list(structure.labels), list(structure.path_connections)
    ends, start = [], labels.pop(0)
    for connected in connections:
        end = labels.pop(0)
        ends.append((start, end))
        start = end if connected else (labels.pop(0) if labels else None)
    kept = [i for i, q in enumerate(structure.qpoints) if np.allclose(np.asarray(q)[:, 2], 0, atol=1e-9)]
    if not kept:
        raise ValueError("The path has no in-plane (q_z = 0) segment")
    paths, new_connections, new_labels = [], [], []
    for n, i in enumerate(kept):
        if not new_connections or not new_connections[-1]:
            new_labels.append(ends[i][0])
        new_labels.append(ends[i][1])
        paths.append(structure.qpoints[i])
        follows = n + 1 < len(kept) and kept[n + 1] == i + 1 and connections[i]
        new_connections.append(bool(follows))
    phonon.run_band_structure(paths, path_connections=new_connections, labels=new_labels)


def analyse(fc_dir, npoints=101, no_symmetrize=False, in_plane=False, label=None):
    if npoints < 2:
        raise ValueError("npoints must be at least 2")
    fc_dir = Path(fc_dir).resolve()
    if not fc_dir.is_dir():
        raise ValueError(f"Input directory does not exist: {fc_dir}")
    with redirect_stdout(sys.stderr):
        from dephonopy.force_constants import ForceConstants

        fc = ForceConstants(str(fc_dir))
        if not no_symmetrize:
            fc.ensure_symmetry()
        phonon = fc.Phonopy(unit_cell=fc.atoms)
        phonon.auto_band_structure(npoints=npoints)
        if in_plane:
            keep_in_plane(phonon)
        result = band_data(phonon)
    result["summary"].update(input_paths={"fc_dir": str(fc_dir)}, symmetrized=not no_symmetrize,
                             in_plane=in_plane, label=label if label is not None else fc_dir.name)
    return result


def write_result(result, directory):
    directory = Path(directory)
    (directory / "summary.json").write_text(
        json.dumps(result["summary"], indent=2, ensure_ascii=False, allow_nan=False) + "\n",
        encoding="utf-8")
    header = "segment\tdistance\t" + "\t".join(
        f"band_{i}" for i in range(result["summary"]["n_bands"]))
    np.savetxt(directory / "dispersion.tsv", result["dispersion"], delimiter="\t", header=header,
               comments="", fmt=["%d"] + ["%.12g"] * (result["summary"]["n_bands"] + 1))
    np.savetxt(directory / "qpoints.tsv", result["qpoints"], delimiter="\t", header="qx\tqy\tqz",
               comments="", fmt="%.12g")


def main():
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise ValueError(message)

    parser = Parser(description=__doc__)
    parser.add_argument("--fc-dir", required=True, type=Path)
    parser.add_argument("--npoints", type=int, default=101)
    parser.add_argument("--no-symmetrize", action="store_true")
    parser.add_argument("--in-plane", action="store_true",
                        help="only q_z = 0 segments (2D slab with vacuum along z)")
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--label")
    try:
        args = vars(parser.parse_args())
        out_dir = args.pop("out_dir")
        result = analyse(**args)
        out_dir = publish(result, out_dir, writer=write_result)
        summary = result["summary"]
        print(json.dumps({"ok": True, "out_dir": str(out_dir),
                          **{key: summary[key] for key in
                             ("n_bands", "min_freq_meV", "max_freq_meV")}}))
        return 0
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
