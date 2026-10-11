"""Reproduce the BSc-thesis defect formation energies (diamond) from raw VASP outputs.

E_f[X^q](E_F) = E_tot[X^q] - E_tot[bulk] - sum_i n_i mu_i + q (E_VBM + E_F) + E_corr[X^q]
Conventions: skill `defect-formation-energy`. E_corr per charged case comes from a CSV with
columns tag (`<defect>_q<+-n>`) and E_corr_eV - sxdefectalign (path A) or the app (path B).

  python scripts/python/formation_energy_repro.py --ecorr <csv> --out <dir> [--ecorr-thesis-c]
  python scripts/python/formation_energy_repro.py --selftest
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
DATA = REPO / "test-directory/fnv-diamond/okeanos/data"
THESIS = Path("D:/STUDIA/Fizyka/01_Engeenering-Studies/Praca-Inżynierska/Engeenering-thesis")
THESIS_IMG = THESIS / "Theisis/theisis/img/Defects"
THESIS_JSON = THESIS / "defects_creation_energy.json"
MU_RUNS = {  # user-confirmed 2026-10-10, HSE, ENCUT 600
    "Ge": Path("O:/Potencjaly_chemiczne/Ge/Lattice_Constant_Convergance/first"),
    "Si": Path("O:/Potencjaly_chemiczne/Si/Lattice_Constant_Convergance/first"),
    "N": Path("O:/Potencjaly_chemiczne/N/first"),
}
# thesis figure -> defects drawn on it (Ge blue, Si green, N_C orange as in the thesis)
FIGURES = {
    "G4V": (["GeV", "SiV"], "Formations_Energy/G4V - formation.png", "Transition_Energy/G4V - transition.png"),
    "G4N": (["GeN", "SiN"], "Formations_Energy/G4N - formation.png", "Transition_Energy/G4N - transition.png"),
    "G4NV": (["GeNV", "SiNV"], "Formations_Energy/G4NV - formation.png", "Transition_Energy/G4NV - transition.png"),
    "G4N2V": (["GeN2V", "SiN2V"], "Formations_Energy/G4N2V - formation.png", "Transition_Energy/G4N2V - transition.png"),
    "Ge_C": (["Ge_C", "Si_C"], "Formations_Energy/Ge_C-formation.png", None),
    "N_C": (["N_C"], "Formations_Energy/N_C_Formation_energy.png", "Transition_Energy/N_C - transition.png"),
}
# found 2026-10-10: SiNV q0 thesis - ours = +0.294021 eV = mu_N - mu_C exactly
THESIS_NOTES = {"SiNV": "\nbłąd w inż: +0.294 eV = μ_N − μ_C"}
# charge states the thesis text keeps per defect (energie_tworzenia_german_azot_wakans.tex);
# the others are computed and drawn dotted, but left out of the envelope and the levels
THESIS_STATES = {"GeV": [1, 0, -1, -2], "GeN": [1, 0, -1], "SiN": [1, -1], "GeNV": [1, 0, -1],
                 "SiNV": [1, 0, -1], "GeN2V": [1, 0], "SiN2V": [1, 0], "Ge_C": [0], "Si_C": [0],
                 "N_C": [1, 0, -1]}
COLOR = {"Ge": "#0000ff", "Si": "#00ff00", "N": "#ff8000"}
EF = np.round(np.arange(0.0, 5.47 + 1e-9, 0.01), 2)  # grid of defects_creation_energy.json


def toten(run):
    lines = [l for l in (run / "OUTCAR").read_text(errors="replace").splitlines() if "free  energy   TOTEN" in l]
    return float(lines[-1].split()[-2])


def nelect(run):
    for line in (run / "OUTCAR").open(errors="replace"):
        if "NELECT" in line:
            return float(line.split()[2])
    raise ValueError(f"no NELECT in {run}")


def composition(run):
    lines = (run / "CONTCAR").read_text().splitlines()
    return dict(zip(lines[5].split(), map(int, lines[6].split())))


def band_edges(run):
    """(VBM, CBM) at the first k-point (Gamma) of EIGENVAL, any spin."""
    lines = (run / "EIGENVAL").read_text().splitlines()
    nbands = int(lines[5].split()[2])
    occ, unocc = [], []
    for row in lines[8:8 + nbands]:
        cols = list(map(float, row.split()[1:]))
        half = len(cols) // 2  # ISPIN=1: e, occ; ISPIN=2: e_up, e_dn, occ_up, occ_dn
        for e, o in zip(cols[:half], cols[half:]):
            (occ if o > 0.5 else unocc).append(e)
    return max(occ), min(unocc)


def load_ecorr(path):
    with open(path, newline="") as f:
        return {r["tag"]: float(r["E_corr_eV"]) for r in csv.DictReader(f)}


def formation(defect, mu, bulk_e, vbm, ecorr):
    """{q: (E_f at E_F=0, run info)} for every q folder of the defect."""
    folders = sorted((DATA / defect).glob("q*"))
    nel0 = nelect(DATA / defect / "q0")
    bulk_c = 512
    out = {}
    for run in folders:
        q = int(round(nel0 - nelect(run)))
        assert run.name == f"q{q:+d}".replace("+0", "0"), f"{run}: NELECT says q={q}"
        comp = composition(run)
        n = {el: c for el, c in comp.items() if el != "C"}
        n["C"] = comp.get("C", 0) - bulk_c
        e_corr = 0.0 if q == 0 else ecorr[f"{defect}_q{q:+d}"]
        e_tot = toten(run)
        ef0 = e_tot - bulk_e - sum(c * mu[el] for el, c in n.items()) + q * vbm + e_corr
        out[q] = dict(E_tot=e_tot, n=n, E_corr=e_corr, Ef0=ef0)
    return out


def kept(defect, states):
    return {q: s for q, s in states.items() if q in THESIS_STATES.get(defect, states)}


def envelope(states, ef=EF):
    lines = np.array([s["Ef0"] + q * ef for q, s in states.items()])
    return lines.min(axis=0), np.array(list(states))[lines.argmin(axis=0)]


def transitions(states):
    """eps(q/q-1) for adjacent computed charge states (thesis eq. transition-energy)."""
    qs = sorted(states, reverse=True)
    return {f"{a}/{b}": (states[b]["Ef0"] - states[a]["Ef0"]) / (a - b) for a, b in zip(qs, qs[1:])}


def thesis_style(ax, ylabel):
    ax.set_xlim(-0.2, 5.7)
    ax.set_ylim(0, 12)
    ax.set_xlabel(r"Poziom Fermiego $E_F$ [eV]", fontsize=14)
    ax.set_ylabel(ylabel, fontsize=14)
    ax.grid(True, color="#999999", lw=0.6)
    ax.tick_params(labelsize=12, direction="in", top=True, right=True)


def plot_family(name, defects, results, ref, out, label):
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(6.4, 4.9), dpi=130)
    thesis_style(ax, "Energia tworzenia [eV]")
    for d in defects:
        color = COLOR["N" if d == "N_C" else d[:2]]
        if d in results:
            for q, s in results[d].items():
                ax.plot(EF, s["Ef0"] + q * EF, color=color, lw=0.8, alpha=0.4,
                        ls="-" if q in kept(d, results[d]) else ":")
            env, _ = envelope(kept(d, results[d]))
            ax.plot(EF, env, color=color, lw=3.5, label=f"{d} ({label})")
        if d in ref:
            ax.plot(EF, ref[d], color="black", lw=1.4, ls="--", label=f"{d} (inż, JSON){THESIS_NOTES.get(d, '')}")
    ax.legend(fontsize=9, loc="upper right")
    ax.set_title(f"{name}: reprodukcja ({label})\ncienkie = pojedyncze q, kropkowane = stan pominięty w inż", fontsize=9)
    fig.tight_layout()
    path = out / f"{name}_formation.png"
    fig.savefig(path)
    plt.close(fig)
    return path


def plot_transitions(name, defects, results, gap, out, label):
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(4.9, 5.1), dpi=130)
    ax.axhspan(-0.5, 0, color="#c4c4f8")
    ax.axhspan(gap, 6, color="#fcc4c4")
    ax.axhline(0, color="blue", lw=1.5)
    ax.axhline(gap, color="red", lw=1.5)
    ax.text(len(defects) + 0.05, gap, "CB", va="center", fontsize=12)
    ax.text(len(defects) + 0.05, 0, "VB", va="center", fontsize=12)
    for i, d in enumerate(defects):
        if d not in results:
            ax.text(i + 0.5, 3, f"{d}\nbrak danych", ha="center", fontsize=10)
            continue
        color = COLOR["N" if d == "N_C" else d[:2]]
        ax.text(i + 0.4, 5.0, d, ha="center", fontsize=11, weight="bold", bbox=dict(fc="white", ec="black"))
        for key, eps in transitions(kept(d, results[d])).items():
            ax.plot([i + 0.1, i + 0.7], [eps, eps], color=color, lw=3.5)
            ax.text(i + 0.72, eps, f"{key}  {eps:.2f}", va="center", fontsize=9)
    ax.set_xlim(0, len(defects) + 0.4)
    ax.set_ylim(-0.5, 6)
    ax.set_xticks([])
    ax.set_ylabel(r"Energie przejścia $\varepsilon$ [eV]", fontsize=14)
    ax.grid(True, axis="y", color="#999999", lw=0.6)
    ax.set_title(f"{name}: poziomy przejść ({label}), E_g = {gap:.2f} eV", fontsize=10)
    fig.tight_layout()
    path = out / f"{name}_transition.png"
    fig.savefig(path)
    plt.close(fig)
    return path


def side_by_side(thesis_png, ours_png, path, title):
    import matplotlib.pyplot as plt
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None
    left = Image.open(thesis_png).convert("RGB")
    left.thumbnail((1400, 1400))
    right = Image.open(ours_png).convert("RGB")
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.4), dpi=110)
    for ax, img, t in zip(axes, (left, right), ("praca inżynierska", "reprodukcja")):
        ax.imshow(img)
        ax.set_title(t, fontsize=12)
        ax.axis("off")
    fig.suptitle(title, fontsize=13)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def run(ecorr_csv, out, label):
    out.mkdir(parents=True, exist_ok=True)
    bulk = DATA / "bulk"
    bulk_e = toten(bulk)
    vbm, cbm = band_edges(bulk)
    mu = {"C": bulk_e / 512, **{el: toten(p) / sum(composition(p).values()) for el, p in MU_RUNS.items()}}
    ecorr = load_ecorr(ecorr_csv)
    ref = json.loads(THESIS_JSON.read_text(encoding="utf-8"))
    ref_ef = np.round(np.array(ref.pop("Ef"), dtype=float), 2)
    # SiV in the thesis JSON is one point shorter than Ef - pair from the start, NaN past its end
    ref = {k: np.interp(EF, ref_ef[:len(v)], np.array(v, dtype=float), right=np.nan) for k, v in ref.items()}
    ref.pop("SiV")  # draft in the JSON: does not match the thesis figure G4V - formation.png

    defects = [d.name for d in sorted(DATA.iterdir()) if d.is_dir() and d.name != "bulk"]
    results = {d: formation(d, mu, bulk_e, vbm, ecorr) for d in defects}

    rows, summary = [], dict(label=label, ecorr_csv=str(ecorr_csv), E_bulk=bulk_e, E_VBM=vbm,
                             E_CBM=cbm, gap=cbm - vbm, mu=mu, mu_source={k: str(v) for k, v in MU_RUNS.items()},
                             defects={}, vs_thesis={})
    for d, states in results.items():
        summary["defects"][d] = dict(states={str(q): s for q, s in states.items()}, kept=sorted(kept(d, states)),
                                     transitions=transitions(kept(d, states)),
                                     transitions_all_states=transitions(states))
        for q, s in states.items():
            rows.append([d, q, f"{s['E_tot']:.8f}", " ".join(f"{k}{v:+d}" for k, v in s["n"].items()),
                         f"{s['E_corr']:.6f}", f"{s['Ef0']:.6f}"])
        if d in ref:
            env, _ = envelope(kept(d, states))
            diff = env - ref[d]
            i = int(np.abs(diff).argmax())
            summary["vs_thesis"][d] = dict(max_abs_diff_eV=float(abs(diff[i])), at_EF=float(EF[i]),
                                           mean_diff_eV=float(diff.mean()))
    with open(out / "formation_table.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["defect", "q", "E_tot_eV", "n_i", "E_corr_eV", "E_f_at_EF0_eV"])
        w.writerows(rows)
    (out / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")

    for name, (fam, form_png, trans_png) in FIGURES.items():
        ours = plot_family(name, fam, results, ref, out, label)
        side_by_side(THESIS_IMG / form_png, ours, out / f"{name}_formation_compare.png",
                     f"{name} - energia tworzenia")
        if trans_png:
            ours = plot_transitions(name, fam, results, cbm - vbm, out, label)
            side_by_side(THESIS_IMG / trans_png, ours, out / f"{name}_transition_compare.png",
                         f"{name} - poziomy przejść")
    bundle_pdf(out, label)
    return summary


def bundle_pdf(out, label):
    """All *_compare.png of one run in one PDF, formation first, in thesis chapter order."""
    from PIL import Image
    pages = [out / f"{n}_{kind}_compare.png" for n in ("N_C", "Ge_C", "G4V", "G4N", "G4NV", "G4N2V")
             for kind in ("formation", "transition")]
    images = [Image.open(p).convert("RGB") for p in pages if p.exists()]
    images[0].save(out / "porownanie_z_inz.pdf", save_all=True, append_images=images[1:], title=label)


def selftest():
    states = {1: dict(Ef0=7.0), 0: dict(Ef0=8.0), -1: dict(Ef0=10.0)}
    assert transitions(states) == {"1/0": 1.0, "0/-1": 2.0}
    env, q = envelope(states, np.array([0.0, 1.5, 3.0]))
    assert np.allclose(env, [7.0, 8.0, 7.0]) and list(q) == [1, 0, -1]
    print("ok")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--ecorr", type=Path)
    p.add_argument("--out", type=Path)
    p.add_argument("--label", default="A: sxdefectalign")
    p.add_argument("--selftest", action="store_true")
    a = p.parse_args()
    if a.selftest:
        selftest()
    else:
        s = run(a.ecorr, a.out, a.label)
        print(json.dumps({k: s[k] for k in ("E_VBM", "E_CBM", "gap", "mu", "vs_thesis")}, indent=1))
        for d, v in s["defects"].items():
            print(d, {k: round(x, 3) for k, x in v["transitions"].items()},
                  {q: round(st["Ef0"], 4) for q, st in v["states"].items()})
