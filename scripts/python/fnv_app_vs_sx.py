"""Path B vs path A: DefectStudio FNV engine (headless batch test) against sxdefectalign.

  python scripts/python/fnv_app_vs_sx.py <B dir with ecorr_app.csv + <tag>_profiles.csv> \
      <A results.csv> <A runs dir with <tag>/vline-eV-a*.dat> [--tol-mev 1]

Per case: max |dV_app - dV_sx| and |V_model_app - V_model_sx| point by point (same LOCPOT bytes
=> ~1e-6 eV), E_lat, auto C, E_corr, and E_corr of the app with sxdefectalign's C (pure E_lat
check). Writes compare.csv and compare.png next to ecorr_app.csv; exit 1 if any case fails.
"""
import argparse
import csv
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from fnv_sxdefectalign import read_vline  # noqa: E402


def rows(path):
    with open(path, newline="") as f:
        return {r["tag"]: r for r in csv.DictReader(f)}


def profiles(path):
    data = np.loadtxt(path, delimiter=",", skiprows=1)
    return {a: data[data[:, 0] == a] for a in range(3)}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("b_dir", type=Path)
    p.add_argument("a_csv", type=Path)
    p.add_argument("a_runs", type=Path)
    p.add_argument("--tol-mev", type=float, default=1.0)
    a = p.parse_args()

    app, sx = rows(a.b_dir / "ecorr_app.csv"), rows(a.a_csv)
    out = []
    for tag, b in app.items():
        s, q = sx[tag], int(b["q"])
        prof = profiles(a.b_dir / f"{tag}_profiles.csv")
        dv_max = vm_max = 0.0
        for ax in range(3):
            _, vm_sx, dv_sx, _ = read_vline(a.a_runs / tag / f"vline-eV-a{ax}.dat")
            dv_max = max(dv_max, float(np.abs(prof[ax][:, 2] - dv_sx).max()))
            vm_max = max(vm_max, float(np.abs(prof[ax][:, 3] - vm_sx).max()))
        e_lat_b, e_lat_a = float(b["E_lat_eV"]), float(s["E_lat_eV"])
        c_b, c_a = float(b["C_eV"]), float(s["C_eV"])
        e_b, e_a = float(b["E_corr_eV"]), float(s["E_corr_eV"])
        r = dict(tag=tag, q=q, max_dV_meV=1e3 * dv_max, max_Vmodel_meV=1e3 * vm_max,
                 dE_lat_meV=1e3 * (e_lat_b - e_lat_a), dC_meV=1e3 * (c_b - c_a),
                 dE_corr_meV=1e3 * (e_b - e_a), dE_corr_sameC_meV=1e3 * (e_lat_b + q * c_a - e_a),
                 E_corr_app=e_b, E_corr_sx=e_a, ecut_app=float(b["energyCutoff_eV"]))
        r["status"] = "PASS" if abs(r["dE_corr_sameC_meV"]) <= a.tol_mev and abs(r["dE_corr_meV"]) <= a.tol_mev else "FAIL"
        out.append(r)

    with open(a.b_dir / "compare.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=out[0].keys())
        w.writeheader()
        w.writerows(out)
    print(f"{'case':<12}{'q':>3}{'maxΔV':>9}{'maxVmod':>9}{'ΔE_lat':>9}{'ΔC':>9}{'ΔE_corr':>9}{'ΔE(C_sx)':>10}  [meV]")
    for r in out:
        print(f"{r['tag']:<12}{r['q']:>3}{r['max_dV_meV']:>9.4f}{r['max_Vmodel_meV']:>9.4f}{r['dE_lat_meV']:>9.4f}"
              f"{r['dC_meV']:>9.4f}{r['dE_corr_meV']:>9.4f}{r['dE_corr_sameC_meV']:>10.4f}  {r['status']}")

    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(10, 4.2), dpi=130)
    x = np.arange(len(out))
    ax.axhspan(-a.tol_mev, a.tol_mev, color="#1baf7a", alpha=0.15, label=f"tolerancja ±{a.tol_mev:g} meV")
    ax.bar(x - 0.2, [r["dE_corr_meV"] for r in out], 0.4, color="#2a78d6", label="E_corr: aplikacja − sxdefectalign (auto C obu)")
    ax.bar(x + 0.2, [r["dE_corr_sameC_meV"] for r in out], 0.4, color="#eb6834", label="to samo przy C z sxdefectalign")
    ax.axhline(0, color="black", lw=0.8)
    ax.set_xticks(x, [r["tag"] for r in out], rotation=60, ha="right", fontsize=8)
    ax.set_ylabel("różnica [meV]")
    ax.set_title("Ścieżka B (moduł FNV aplikacji) vs ścieżka A (sxdefectalign), te same LOCPOT, ε = 5.7")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(a.b_dir / "compare.png")
    return 0 if all(r["status"] == "PASS" for r in out) else 1


if __name__ == "__main__":
    raise SystemExit(main())
