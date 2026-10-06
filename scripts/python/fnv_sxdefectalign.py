"""FNV (Freysoldt-Neugebauer-Van de Walle) charge correction with sxdefectalign.

Two-pass workflow from the BSc thesis (Notatki/Charakteryzacja-defektow.md), with the plateau
proposed automatically instead of read by hand in Origin:

  run  <case.json> [--out DIR]   pass 1 (no -C) -> plateau proposal -> pass 2 (-C <plateau>)
  plot <dir> [--out DIR]         plateau proposal + figure from existing vline-eV-a*.dat

case.json (paths relative to the file):
  {"name": "GeN q=-1", "q": -1, "eps": 5.7, "eps_source": "promotor", "ecut_ry": 44.11,
   "center": [0.46124, 0.559967, 0.461206], "vdef": "LOCPOT_1-", "vref": "LOCPOT-bulk",
   "sxdefectalign": "../../bin/sxdefectalign", "C": null}

`q` is the VASP charge state; sxdefectalign takes the excess-electron count, so --charge = -q.
`C` set -> use it in pass 2 instead of the proposal (manual plateau, as in the thesis).
sxdefectalign is a Linux binary: it runs through `wsl.exe -d Ubuntu`.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

COLORS = {"model": "#2a78d6", "dft": "#eb6834", "resid": "#1baf7a"}  # dataviz slots 1-3


def read_vline(path):
    """vline-eV-aN.dat: block 1 = (z, V_model), '&', block 2 = (z, dV_DFT, dV_DFT - V_model)."""
    text = Path(path).read_text().split("&")
    model = np.loadtxt(text[0].splitlines())
    dft = np.loadtxt(text[1].splitlines())
    return model[:, 0], model[:, 1], dft[:, 1], dft[:, 2]


def propose_window(z, v_model, resid, min_width, tol):
    """Widest window centred on the point farthest from the defect with std(resid) <= tol."""
    L = z[1] * len(z)  # grid excludes the periodic endpoint
    z0 = z[np.argmax(np.abs(v_model))]  # model potential peaks at the defect
    dist = np.abs((z - z0 + L / 2) % L - L / 2)
    pick = None
    for width in np.arange(min_width, L / 2 + 1e-9, z[1]):
        mask = dist >= L / 2 - width / 2
        if resid[mask].std() <= tol:
            pick = (width, mask)
    flat = pick is not None
    width, mask = pick if flat else (min_width, dist >= L / 2 - min_width / 2)
    return {"defect_z": float(z0), "L": float(L), "width": float(width), "flat": flat,
            "C": float(resid[mask].mean()), "std": float(resid[mask].std()),
            "mask": mask, "dist": dist}


def model_slope(v_model, dv, outside):
    """Slope of dV_DFT vs V_model outside the defect core: ~1 = localized charge (FNV valid),
    <1 = partly delocalized (model over-corrects), ~0 = delocalized (FNV meaningless)."""
    return float(np.polyfit(v_model[outside], dv[outside], 1)[0])


def analyse(folder, min_width=4.0, tol=0.01):
    folder = Path(folder)
    axes = []
    for a in range(3):
        z, vm, dv, res = read_vline(folder / f"vline-eV-a{a}.dat")
        w = propose_window(z, vm, res, min_width, tol)
        w["slope"] = model_slope(vm, dv, w["dist"] > 5.0)  # ponytail: 5 bohr core, fixed
        axes.append({"z": z, "v_model": vm, "dv": dv, "resid": res, **w})
    Cs = np.array([ax["C"] for ax in axes])
    out = {"C": float(Cs.mean()), "C_axis_spread": float(Cs.max() - Cs.min()),
           "slope": float(np.mean([ax["slope"] for ax in axes])),
           "axes": [{k: ax[k] for k in ("defect_z", "L", "width", "flat", "C", "std", "slope")}
                    for ax in axes]}
    atoms = folder / "vAtoms.dat"
    if atoms.exists():
        # columns: r, V_lr, dV, dV - V_lr, x, y, z (bohr, eV)
        v = np.loadtxt(atoms)
        far = v[:, 0] >= 0.75 * v[:, 0].max()  # ponytail: fixed outer shell, tune if needed
        out["atoms_far"] = {"r_min": float(v[far, 0].min()), "n": int(far.sum()),
                            "C": float(v[far, 3].mean()), "std": float(v[far, 3].std()),
                            "slope": model_slope(v[:, 1], v[:, 2], v[:, 0] > 8.0)}
        return out, axes, (v, far)
    return out, axes, None


def plot(summary, axes, atoms, title, png):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    n = 4 if atoms is not None else 3
    fig, grid = plt.subplots(1, n, figsize=(4.2 * n, 3.8), constrained_layout=True)
    for a, (ax, d) in enumerate(zip(grid, axes)):
        ax.plot(d["z"], d["v_model"], color=COLORS["model"], lw=2, label="V_model")
        ax.plot(d["z"], d["dv"], color=COLORS["dft"], lw=2, label="ΔV DFT")
        ax.plot(d["z"], d["resid"], color=COLORS["resid"], lw=2, label="ΔV − V_model")
        ax.fill_between(d["z"], 0, 1, where=d["mask"], transform=ax.get_xaxis_transform(),
                        color="0.85", lw=0, zorder=0, label="plateau")
        ax.axhline(d["C"], color="0.3", lw=1, ls="--")
        ax.axvline(d["defect_z"], color="0.6", lw=1, ls=":")
        flag = "" if d["flat"] else "  (brak plateau!)"
        ax.set_title(f"oś a{a}: C = {d['C']:+.4f} eV, σ = {d['std']:.4f}, "
                     f"ΔV/V_model = {d['slope']:.2f}{flag}", fontsize=9)
        ax.set_xlabel("z [bohr]")
        ax.grid(alpha=0.25)
    grid[0].set_ylabel("potencjał [eV]")
    grid[0].legend(fontsize=8, loc="best")
    if atoms is not None:
        v, far = atoms
        ax = grid[3]
        ax.scatter(v[~far, 0], v[~far, 3], s=10, color=COLORS["resid"], alpha=0.5,
                   label="atomy")
        ax.scatter(v[far, 0], v[far, 3], s=10, color="0.25", label="daleko (średnia)")
        ax.axhline(summary["atoms_far"]["C"], color="0.3", lw=1, ls="--")
        ax.set_title(f"vAtoms: C = {summary['atoms_far']['C']:+.4f} eV", fontsize=9)
        ax.set_xlabel("r od defektu [bohr]")
        ax.legend(fontsize=8)
        ax.grid(alpha=0.25)
    warn = "" if summary["slope"] > 0.9 else "   ⚠ ładunek zdelokalizowany - FNV niewiarygodne"
    fig.suptitle(f"{title}   C(średnia osi) = {summary['C']:+.4f} eV, "
                 f"rozrzut osi = {summary['C_axis_spread']:.4f} eV{warn}", fontsize=10)
    fig.savefig(png, dpi=130)
    plt.close(fig)


def to_wsl(p):
    p = Path(p).resolve()
    return f"/mnt/{p.drive[0].lower()}" + p.as_posix()[2:]


def sxdefectalign(case, case_dir, workdir, C):
    workdir.mkdir(parents=True, exist_ok=True)
    args = [to_wsl(case_dir / case["sxdefectalign"]),
            "--ecut", str(case["ecut_ry"]), "--charge", str(-case["q"]),
            "--eps", str(case["eps"]), "--center", ",".join(map(str, case["center"])),
            "--relative", "--vdef", to_wsl(case_dir / case["vdef"]),
            "--vref", to_wsl(case_dir / case["vref"]), "--vasp"]
    if C is not None:
        args += ["-C", f"{C:.6f}"]
    r = subprocess.run(["wsl.exe", "-d", "Ubuntu", "--cd", to_wsl(workdir), "--", *args],
                       capture_output=True, text=True)
    (workdir / "sxdefectalign.log").write_text(" ".join(args) + "\n\n" + r.stdout + r.stderr)
    if r.returncode != 0:
        sys.exit(f"sxdefectalign failed ({r.returncode}), see {workdir / 'sxdefectalign.log'}")
    num = r"(-?\d+\.?\d*(?:[eE][-+]?\d+)?)"
    found = {k: re.search(p, r.stdout) for k, p in {
        "E_corr": rf"Defect correction \(eV\):\s*{num}",
        "E_iso_Ha": rf"Isolated energy\s*{num}", "E_per_Ha": rf"Periodic energy\s*{num}",
        "E_lat_unscreened": rf"Difference\s*{num}", "V_average": rf"V average:\s*{num}"}.items()}
    return {k: float(m.group(1)) for k, m in found.items() if m}


def cmd_run(a):
    case_path = Path(a.case).resolve()
    case = json.loads(case_path.read_text(encoding="utf-8"))
    out = Path(a.out or case_path.parent / "run")
    p1 = sxdefectalign(case, case_path.parent, out / "pass1", None)
    s1, axes, atoms = analyse(out / "pass1", a.min_width, a.tol)
    plot(s1, axes, atoms, f"{case['name']} - przebieg 1", out / "pass1.png")
    C = case.get("C")
    C_used = s1["C"] if C is None else C
    p2 = sxdefectalign(case, case_path.parent, out / "pass2", C_used)
    s2, axes, atoms = analyse(out / "pass2", a.min_width, a.tol)
    plot(s2, axes, atoms, f"{case['name']} - przebieg 2 (-C {C_used:+.4f})", out / "pass2.png")
    result = {"case": case, "pass1": {**p1, "plateau": s1},
              "C_used": C_used, "C_source": "auto" if C is None else "manual",
              "pass2": {**p2, "plateau": s2},
              "E_corr": p2.get("E_corr"), "E_lat": p1.get("E_corr")}
    (out / "summary.json").write_text(json.dumps(result, indent=2, ensure_ascii=False),
                                      encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("C_used", "C_source", "E_lat", "E_corr")},
                     indent=2))


def cmd_plot(a):
    folder = Path(a.dir)
    out = Path(a.out or folder)
    out.mkdir(parents=True, exist_ok=True)
    s, axes, atoms = analyse(folder, a.min_width, a.tol)
    plot(s, axes, atoms, a.title or folder.name, out / "plateau.png")
    (out / "plateau.json").write_text(json.dumps(s, indent=2), encoding="utf-8")
    print(json.dumps(s, indent=2))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("run", cmd_run), ("plot", cmd_plot)):
        p = sub.add_parser(name)
        p.add_argument("case" if name == "run" else "dir")
        p.add_argument("--out")
        p.add_argument("--min-width", type=float, default=4.0, help="bohr")
        p.add_argument("--tol", type=float, default=0.01, help="max std in window, eV")
        if name == "plot":
            p.add_argument("--title")
        p.set_defaults(fn=fn)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
