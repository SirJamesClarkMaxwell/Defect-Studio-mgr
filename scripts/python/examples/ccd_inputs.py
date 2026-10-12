#!/usr/bin/env python
"""Configuration-coordinate diagram (CCD) single points between two relaxed geometries.

Generalised from the user's ``sjcm_mgr/scripts/build_ccd.py`` (VV divacancy) - same physics,
same layout, configuration on the command line instead of constants (task 104e).

The ground-state (GS) and excited-state (ES) relaxed geometries define a straight line in
configuration space, R(t) = R_gs + t (R_es - R_gs), built with
``puntukas.core.compare.interpolate`` (minimum image, centre-of-mass drift removed). t = 0 is the
GS minimum, t = 1 the ES minimum. Two branches of single points under ``--out-dir``:

* ``es/`` - INCAR from the ES run (e.g. ISMEAR=-2, FERWE/FERDO), t over ``--es-range``,
* ``gs/`` - INCAR from the GS run, t over ``--gs-range``.

Every point restarts from the WAVECAR of its neighbour closer to the branch minimum (ISTART=1);
the minimum (seed: ``es/t_+100``, ``gs/t_+000``) gets its WAVECAR by hand. Each branch has one
``run.job`` running the chain seed -> up in t -> down in t, deleting intermediate WAVECARs once
no later point needs them. ``ccd_path.csv`` lists t and Q = t * dQ, dQ = sqrt(sum m |dr|^2)
in amu^1/2 Angstrom. All generated files are LF + ASCII.

stdout ends with one JSON line: {"ok": true, "out_dir", "points", "delta_Q_amu05_A"} or
{"ok": false, "error"} (exit 1).
"""

import argparse
import json
import sys
from contextlib import redirect_stdout
from pathlib import Path

import numpy as np

DEFAULT_JOB_HEADER = """#!/usr/bin/env bash
#SBATCH --ntasks=52
#SBATCH --job-name="{job_name}"
#SBATCH --output="%j.log"
#SBATCH --error="%j.err"

date +'%Y-%m-%dT%H:%M:%S%z'

module load vasp/6.6.1-oneapi2024.2
ulimit -s unlimited

# After `module load`: module scripts do not survive `set -e`.
# Stop the chain on the first failure instead of feeding a broken WAVECAR on.
set -e

VASP_EXEC="vasp_gam"
MPIRUN_COMMAND="mpirun -np 52"
"""

JOB_FOOTER = """
date +'%Y-%m-%dT%H:%M:%S%z'
"""


def fractions(first, last, step):
    n = round((last - first) / step)
    return [round(first + i * step, 6) for i in range(n + 1)]


def dirname_for(t):
    """-0.75 -> 't_-075', 0.0 -> 't_+000', 1.25 -> 't_+125'."""
    return f"t_{round(t * 100):+04d}"


def chain(ts, seed):
    """Run order as (t, parent t): seed first, then up in t, then down."""
    if seed not in ts:
        raise ValueError(f"seed t={seed} is not on the grid {ts}")
    up = [t for t in ts if t > seed]
    down = [t for t in reversed(ts) if t < seed]
    order = [(seed, None)]
    for side in (up, down):
        parent = seed
        for t in side:
            order.append((t, parent))
            parent = t
    return order


def job_script(header, job_name, order):
    """run.job text: one block per point, WAVECARs removed once unused."""
    seed = order[0][0]
    last_use = {parent: i for i, (_, parent) in enumerate(order) if parent is not None}
    lines = [header.format(job_name=job_name)]
    for i, (t, parent) in enumerate(order):
        d = dirname_for(t)
        if parent is None:
            lines += [
                f"# t = {t:+.2f}: seed, WAVECAR provided by hand",
                f"pushd {d}",
                f'[ -f WAVECAR ] || {{ echo "missing seed WAVECAR in {d}" >&2; exit 1; }}',
            ]
        else:
            p = dirname_for(parent)
            lines += [f"# t = {t:+.2f} <- {p}", f"pushd {d}", f"cp ../{p}/WAVECAR ."]
        lines.append("$MPIRUN_COMMAND $VASP_EXEC")
        if parent is not None and parent != seed and last_use[parent] == i:
            lines.append(f"rm ../{dirname_for(parent)}/WAVECAR")
        lines += ["popd", ""]
    lines.append(JOB_FOOTER)
    return "\n".join(lines)


def write_lf(path, text):
    path.write_bytes(text.replace("\r\n", "\n").encode("ascii"))


def force_lf_ascii(directory):
    """Rewrite every file in ``directory`` with LF endings; refuse non-ASCII."""
    for f in directory.iterdir():
        data = f.read_bytes()
        bad = next((i for i, b in enumerate(data) if b > 127), None)
        if bad is not None:
            raise ValueError(f"{f}: non-ASCII byte at offset {bad}")
        if b"\r" in data:
            f.write_bytes(data.replace(b"\r\n", b"\n"))


def single_point(source_dir, nelm):
    """VaspInput from a finished run, turned into a WAVECAR-restarted single point."""
    from puntukas.vasp import VaspInput

    calc = VaspInput.from_directory(source_dir, use_contcar=True)
    calc.setup_relaxation(False, False, False)  # NSW=0, IBRION=-1, no EDIFFG
    calc.incar.reset("POTIM")
    calc.incar.ISTART = 1
    calc.incar.update(NELM=nelm)
    return calc


def generate(gs_dir, es_dir, out_dir, step=0.25, es_range=(0.0, 1.75), gs_range=(-0.75, 1.5),
             nelm=300, job_name_prefix="CCD", job_header=DEFAULT_JOB_HEADER):
    from puntukas.core.compare import atomic_displacements, interpolate
    from puntukas.vasp import VaspInput

    gs_dir, es_dir, out_dir = Path(gs_dir), Path(es_dir), Path(out_dir)
    if step <= 0:
        raise ValueError("step must be positive")
    if out_dir.exists() and any(out_dir.iterdir()):
        raise FileExistsError(f"Target exists and is not empty: {out_dir}")
    branches = {"es": (es_dir, *es_range, 1.0), "gs": (gs_dir, *gs_range, 0.0)}

    gs_atoms = VaspInput.from_directory(gs_dir, use_contcar=True).atoms
    es_atoms = VaspInput.from_directory(es_dir, use_contcar=True).atoms
    displ = atomic_displacements(gs_atoms, es_atoms)
    delta_q = float(np.sqrt(gs_atoms.masses @ (displ ** 2).sum(axis=1)))
    print(f"GS: {gs_dir}\nES: {es_dir}")
    print(f"dQ = {delta_q:.4f} amu^1/2 A, max |dr| = {np.linalg.norm(displ, axis=1).max():.4f} A")

    rows = ["branch,t,Q_amu05_A"]
    for branch, (source, first, last, seed) in branches.items():
        ts = fractions(first, last, step)
        images = dict(zip(ts, interpolate(gs_atoms, es_atoms, ts)))
        calc = single_point(source, nelm)
        branch_dir = out_dir / branch
        print(f"\n{branch}: {len(ts)} points, seed {dirname_for(seed)}")
        for t in ts:
            point_dir = branch_dir / dirname_for(t)
            calc.atoms = images[t]
            calc.write_input(point_dir)
            force_lf_ascii(point_dir)
            rows.append(f"{branch},{t:.4f},{t * delta_q:.6f}")
            print(f"  {point_dir.name}: t = {t:+.2f}, Q = {t * delta_q:+.4f}")
        write_lf(branch_dir / "run.job", job_script(job_header, f"{job_name_prefix}_{branch}", chain(ts, seed)))

    # The endpoints must reproduce the relaxed geometries (up to the ~1e-5 A centre-of-mass drift
    # interpolate removes).
    for branch, t, ref in (("gs", 0.0, gs_atoms), ("es", 1.0, es_atoms)):
        written = VaspInput.from_directory(out_dir / branch / dirname_for(t)).atoms
        if np.abs(atomic_displacements(ref, written, align_com=False)).max() >= 1e-4:
            raise RuntimeError(f"{branch}/{dirname_for(t)} does not match its relaxed geometry")

    write_lf(out_dir / "ccd_path.csv", "\n".join(rows) + "\n")
    print(f"\nWrote {len(rows) - 1} single points to {out_dir}")
    print("Before sbatch: put the seed WAVECARs into "
          + ", ".join(f"{b}/{dirname_for(s)}" for b, (_, _, _, s) in branches.items()))
    return {"out_dir": str(out_dir.resolve()), "points": len(rows) - 1, "delta_Q_amu05_A": delta_q}


def demo():
    """Self-check of the grid, the chain order and the WAVECAR clean-up."""
    ts = fractions(0.0, 1.75, 0.25)
    assert ts == [0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75]
    assert fractions(-0.75, 1.5, 0.25)[0] == -0.75 and len(fractions(-0.75, 1.5, 0.25)) == 10
    assert dirname_for(-0.75) == "t_-075" and dirname_for(1.75) == "t_+175"
    order = chain(ts, 1.0)
    assert order[0] == (1.0, None)
    assert [t for t, _ in order[1:4]] == [1.25, 1.5, 1.75]
    assert order[4] == (0.75, 1.0) and order[-1] == (0.0, 0.25)
    job = job_script(DEFAULT_JOB_HEADER, "CCD_es", order)
    assert "\r" not in job
    assert "rm ../t_+100/WAVECAR" not in job  # seed is kept
    assert "rm ../t_+175/WAVECAR" not in job  # chain ends are kept
    assert "rm ../t_+125/WAVECAR" in job      # intermediates go
    assert job.index("cp ../t_+125/WAVECAR") < job.index("rm ../t_+125/WAVECAR")
    print("demo: OK")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--gs-dir", type=Path)
    parser.add_argument("--es-dir", type=Path)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--step", type=float, default=0.25)
    parser.add_argument("--es-range", type=float, nargs=2, default=(0.0, 1.75), metavar=("FIRST", "LAST"))
    parser.add_argument("--gs-range", type=float, nargs=2, default=(-0.75, 1.5), metavar=("FIRST", "LAST"))
    parser.add_argument("--nelm", type=int, default=300)
    parser.add_argument("--job-name-prefix", default="CCD")
    parser.add_argument("--job-header", type=Path, help="file with the run.job header; '{job_name}' is substituted")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        demo()
        return 0
    try:
        if not (args.gs_dir and args.es_dir and args.out_dir):
            raise ValueError("--gs-dir, --es-dir and --out-dir are required")
        header = args.job_header.read_text(encoding="utf-8") if args.job_header else DEFAULT_JOB_HEADER
        with redirect_stdout(sys.stderr):
            result = generate(args.gs_dir, args.es_dir, args.out_dir, args.step, tuple(args.es_range),
                              tuple(args.gs_range), args.nelm, args.job_name_prefix, header)
        print(json.dumps({"ok": True, **result}))
        return 0
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
