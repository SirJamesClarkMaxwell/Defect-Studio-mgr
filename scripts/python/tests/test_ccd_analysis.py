"""Task 105b: scripts/python/examples/ccd_analysis.py (1D CCD energies -> harmonic fits, Numerov, PL)."""

import csv
import json
import math
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from uuid import uuid4

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts" / "python" / "examples" / "ccd_analysis.py"
VV = ROOT / "test-directory" / "sjcm_mgr" / "VV" / "CH0" / "R2SCAN" / "8x8x2" / "ccd"
V2CBCN_DIMER = ROOT / "test-directory" / "sjcm_mgr" / "hBN" / "V2CBCN" / "dimer"

# S = C * dQ^2 * hbar_omega  (dQ in amu^1/2 A, hbar_omega in meV); 30 meV, dQ 1 -> S 3.588.
C = 1.66053906660e-47 * 1.602176634e-22 / (2 * 1.054571817e-34 ** 2)


def stiffness(hbar_omega_mev):
    """k in eV / (amu A^2) so that E = k Q^2 / 2."""
    return 2 * C * hbar_omega_mev**2 / 1000


def point_name(t):
    return f"t_{round(t * 100):+04d}"


def write_point(directory, energy, steps=12, nelm=60, final_line=True):
    directory.mkdir(parents=True)
    (directory / "INCAR").write_text(f"ISMEAR = 0\nNELM = {nelm}\nNSW = 0\n", encoding="ascii")
    lines = [f"DAV: {i:4d}    {energy:.10E}   -0.1E-06   -0.1E-08   100   0.1E-03" for i in range(1, steps + 1)]
    if final_line:
        lines.append(f"   1 F= {energy:.8E} E0= {energy:.8E}  d E =0.000000E+00")
    (directory / "OSZICAR").write_text("       N       E                     dE             d eps       ncg     rms\n"
                                       + "\n".join(lines) + "\n", encoding="ascii")
    # Per-SCF-step lines (4 spaces) must never be read as the result; only the final 2/3-space line counts.
    outcar = [f"  free energy    TOTEN  =     {energy + 0.5:.8f} eV" for _ in range(steps)]
    if final_line:
        outcar.append(f"  free  energy   TOTEN  =     {energy:.8f} eV")
    (directory / "OUTCAR").write_text("\n".join(outcar) + "\n", encoding="ascii")


def make_ccd(root, *, omega_gs=30.0, omega_es=30.0, delta_q=1.0, zpl=2.0, e0=-1000.0,
             es_t=(0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5), gs_t=(-0.5, -0.25, 0.0, 0.25, 0.5, 0.75, 1.0)):
    rows = []
    for branch, ts in (("es", es_t), ("gs", gs_t)):
        for t in ts:
            q = t * delta_q
            energy = (e0 + zpl + 0.5 * stiffness(omega_es) * (q - delta_q) ** 2 if branch == "es"
                      else e0 + 0.5 * stiffness(omega_gs) * q**2)
            write_point(root / branch / point_name(t), energy)
            rows.append((branch, f"{t:.4f}", f"{q:.6f}"))
    with (root / "ccd_path.csv").open("w", newline="", encoding="ascii") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("branch", "t", "Q_amu05_A"))
        writer.writerows(rows)


def run(*args):
    proc = subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], capture_output=True, text=True,
                          encoding="utf-8", timeout=600)
    lines = [line for line in proc.stdout.splitlines() if line.strip()]
    return proc, json.loads(lines[-1]) if lines else None


def poisson(s, n):
    return math.exp(-s) * s**n / math.factorial(n)


class CcdAnalysisTests(unittest.TestCase):
    def setUp(self):
        # Python 3.13 mkdtemp's Windows ACL blocks sandbox access; inherit the parent ACL.
        self.tmp = Path(tempfile.gettempdir()) / f"ds-ccd-{uuid4().hex}"
        self.tmp.mkdir()
        self.ccd = self.tmp / "ccd"
        self.out = self.tmp / "out"

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def summary(self):
        return json.loads((self.out / "summary.json").read_text(encoding="utf-8"))

    def assert_view_data(self, omega_gs, omega_es):
        summary = self.summary()
        curves = np.loadtxt(self.out / "ccd_curves.tsv", skiprows=1)
        self.assertGreater(len(curves), 0)
        with (self.out / "wavefunctions.tsv").open(encoding="ascii") as handle:
            header = handle.readline().strip().split("\t")
            waves = np.loadtxt(handle)
        self.assertEqual(header[0], "Q_amu05_A")
        self.assertLessEqual(len(waves), 2001)
        np.testing.assert_allclose(waves[[0, -1], 0], curves[[0, -1], 0])
        for branch, omega in (("gs", omega_gs), ("es", omega_es)):
            levels = summary["numerov"][f"{branch}_levels_meV"]
            count = min(8, len(levels))
            self.assertEqual([h for h in header if h.startswith(branch + "_")],
                             [f"{branch}_{n}" for n in range(count)])
            np.testing.assert_allclose(levels[:count], (np.arange(count) + 0.5) * omega, rtol=1e-3)
        np.testing.assert_allclose(np.trapezoid(waves[:, 1:]**2, waves[:, 0], axis=0), 1, atol=1e-3)
        pl = np.loadtxt(self.out / "pl_1d.tsv", skiprows=1)
        self.assertLess(np.max(np.abs(pl[:, 1] - pl[:, 2])), 1e-2)
        fc = summary["franck_condon"]
        self.assertAlmostEqual(fc["absorption_eV"] - fc["emission_eV"],
                               fc["relaxation_gs_eV"] + fc["relaxation_es_eV"], delta=1e-9)
        self.assertEqual(fc["zpl_eV"], summary["zpl_eV"]["used"])
        self.assertEqual(fc["delta_Q_amu05_A"], summary["delta_Q_amu05_A"])
        with (self.out / "levels.tsv").open(encoding="ascii") as handle:
            self.assertEqual(len(list(csv.DictReader(handle, delimiter="\t"))),
                             sum(len(summary["numerov"][f"{b}_levels_meV"]) for b in ("gs", "es")))

    def test_equal_harmonic_wells_reproduce_poisson(self):
        make_ccd(self.ccd)
        proc, payload = run("--ccd-dir", self.ccd, "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        self.assertTrue(payload["ok"], payload)
        self.assertAlmostEqual(payload["zpl_eV"], 2.0, places=6)
        summary = self.summary()
        self.assertEqual(summary["schema"], "ds.ccd_analysis.v1")
        self.assertAlmostEqual(summary["delta_Q_amu05_A"], 1.0, places=6)
        self.assertAlmostEqual(summary["zpl_eV"]["from_ccd"], 2.0, places=6)
        self.assertAlmostEqual(summary["zpl_eV"]["used"], 2.0, places=6)
        self.assertIsNone(summary["zpl_formula"])
        self.assertEqual(summary["label"], "ccd")
        self.assertEqual(summary["inputs_relative"]["ccd_dir"], "../ccd")
        self.assertIsNone(summary["inputs_relative"]["es_dir"])
        s = C * 30.0
        for branch in ("gs", "es"):
            fit = summary["harmonic"][branch]
            self.assertAlmostEqual(fit["hbar_omega_meV"], 30.0, delta=0.05)
            self.assertAlmostEqual(fit["S"], s, delta=0.01)
            self.assertAlmostEqual(fit["relaxation_eV"], s * 0.030, delta=1e-4)
        numerov = summary["numerov"]
        self.assertAlmostEqual(numerov["gs_levels_meV"][1] - numerov["gs_levels_meV"][0], 30.0, delta=0.3)
        self.assertAlmostEqual(numerov["es_levels_meV"][1] - numerov["es_levels_meV"][0], 30.0, delta=0.3)
        for n in range(10):  # emission: excited v=0 -> ground n
            self.assertAlmostEqual(numerov["fc_0n"][n], poisson(s, n), delta=2e-3, msg=f"n={n}")
        self.assertGreater(numerov["fc_sum"], 0.99)
        for name in ("ccd_points.tsv", "ccd_curves.tsv", "levels.tsv", "pl_1d.tsv"):
            self.assertTrue((self.out / name).is_file(), name)
        with (self.out / "pl_1d.tsv").open(encoding="utf-8") as handle:
            header = handle.readline().rstrip("\n").split("\t")
            rows = [list(map(float, line.split("\t"))) for line in handle if line.strip()]
        self.assertEqual(header, ["photon_energy_eV", "intensity_numerov", "intensity_harmonic",
                                  "intensity_phonon_1d"])
        energies = [row[0] for row in rows]
        self.assertEqual(energies, sorted(energies))
        self.assertGreater(energies[0], 0)
        for column in (1, 2):
            peak = max(rows, key=lambda row: row[column])
            self.assertAlmostEqual(peak[column], 1.0, places=6)
            # The sideband maximum sits about S quanta below the ZPL.
            self.assertAlmostEqual(peak[0], 2.0 - s * 0.030, delta=0.04)
        self.assertTrue(all(math.isnan(row[3]) for row in rows))
        self.assert_view_data(30.0, 30.0)
        self.assertEqual(summary["franck_condon"]["source"], "points")

    def test_unequal_curvatures_give_two_huang_rhys_factors(self):
        make_ccd(self.ccd, omega_gs=30.0, omega_es=40.0)
        proc, payload = run("--ccd-dir", self.ccd, "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        harmonic = self.summary()["harmonic"]
        self.assertAlmostEqual(harmonic["gs"]["hbar_omega_meV"], 30.0, delta=0.05)
        self.assertAlmostEqual(harmonic["es"]["hbar_omega_meV"], 40.0, delta=0.05)
        self.assertAlmostEqual(harmonic["gs"]["S"], C * 30.0, delta=0.01)
        self.assertAlmostEqual(harmonic["es"]["S"], C * 40.0, delta=0.01)

    def test_unconverged_and_unfinished_points_are_flagged_and_excluded(self):
        make_ccd(self.ccd, es_t=(0.0, 0.25, 0.5, 0.75, 1.0, 1.25))
        # Hit NELM: a TOTEN exists but must not be trusted.
        bad = self.ccd / "es" / point_name(1.5)
        write_point(bad, -500.0, steps=60, nelm=60)
        # Killed mid-SCF: only per-step energies in OUTCAR.
        cut = self.ccd / "es" / point_name(1.75)
        write_point(cut, -400.0, steps=20, nelm=60, final_line=False)
        with (self.ccd / "ccd_path.csv").open("a", newline="", encoding="ascii") as handle:
            handle.write("es,1.5000,1.500000\nes,1.7500,1.750000\n")
        proc, payload = run("--ccd-dir", self.ccd, "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        points = {(p["branch"], round(p["t"], 4)): p for p in self.summary()["points"]}
        self.assertEqual(points[("es", 1.5)]["status"], "unconverged")
        self.assertEqual(points[("es", 1.5)]["scf_steps"], 60)
        self.assertEqual(points[("es", 1.5)]["nelm"], 60)
        self.assertEqual(points[("es", 1.75)]["status"], "missing")
        self.assertIsNone(points[("es", 1.75)]["energy_eV"])
        self.assertEqual(points[("es", 1.0)]["status"], "ok")
        # The bad points did not pull the fit.
        self.assertAlmostEqual(self.summary()["harmonic"]["es"]["hbar_omega_meV"], 30.0, delta=0.05)

    def test_zpl_override_shifts_the_excited_curve(self):
        make_ccd(self.ccd)
        proc, payload = run("--ccd-dir", self.ccd, "--zpl", "3.5", "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        zpl = self.summary()["zpl_eV"]
        self.assertAlmostEqual(zpl["from_ccd"], 2.0, places=6)
        self.assertAlmostEqual(zpl["used"], 3.5, places=6)
        self.assertAlmostEqual(payload["zpl_eV"], 3.5, places=6)
        summary = self.summary()
        fc = summary["franck_condon"]
        relaxation = C * 30.0**2 / 1000
        self.assertAlmostEqual(fc["absorption_eV"], 3.5 + relaxation, places=7)
        self.assertAlmostEqual(fc["emission_eV"], 3.5 - relaxation, places=7)
        es_at_gs = next(p for p in summary["points"] if p["branch"] == "es" and p["t"] == 0)
        self.assertAlmostEqual(es_at_gs["energy_eV"], fc["absorption_eV"], places=9)

    def test_constant_formula_precedes_override_and_labels_nested_dataset(self):
        make_ccd(self.ccd)
        self.out = self.ccd / "defectstudio" / "ccd"
        proc, payload = run("--ccd-dir", self.ccd, "--zpl", 3.5,
                            "--zpl-formula", "1.945", "--label", "Eksperyment α", "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        summary = self.summary()
        self.assertEqual(summary["label"], "Eksperyment α")
        self.assertEqual(summary["zpl_eV"]["formula"], 1.945)
        self.assertEqual(payload["zpl_eV"], 1.945)
        self.assertEqual(summary["zpl_formula"], dict(text="1.945", variables_eV={}, value_eV=1.945))
        self.assertEqual(summary["inputs_relative"]["ccd_dir"], "../..")
        self.assertTrue((self.ccd / "ccd_path.csv").is_file())

    def test_too_few_converged_points_is_a_json_error(self):
        make_ccd(self.ccd, gs_t=(0.0, 0.25))
        proc, payload = run("--ccd-dir", self.ccd, "--out-dir", self.out)
        self.assertEqual(proc.returncode, 1)
        self.assertFalse(payload["ok"])
        self.assertIn("gs", payload["error"])
        self.assertFalse(self.out.exists())

    def test_no_input_is_a_json_error(self):
        proc, payload = run("--out-dir", self.out)
        self.assertEqual(proc.returncode, 1)
        self.assertFalse(payload["ok"])

    def test_effective_mode_from_a_phonon_result(self):
        # Two modes: 20 meV with q 0.6, 50 meV with q 0.8 -> dQ 1, omega_eff^2 = 0.36*400 + 0.64*2500.
        phonon = self.tmp / "phonon"
        phonon.mkdir()
        (phonon / "summary.json").write_text(json.dumps({
            "schema": "ds.phonon_vibronic.v1", "delta_Q_amu05_A": 1.0,
            "zpl_eV": {"computed": 3.45, "computed_spin_purified": 3.75, "experimental": None}}), encoding="utf-8")
        (phonon / "modes.tsv").write_text(
            "index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation\n"
            "0\t0.0\tA1\t2\t1\t0\t0.0\t1\n"
            "1\t20.0\tA1\t2\t1\t0.86\t0.6\t0\n"
            "2\t50.0\tA1\t2\t1\t3.83\t-0.8\t0\n", encoding="utf-8")
        proc, payload = run("--phonon-result", phonon, "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        summary = self.summary()
        omega_eff = math.sqrt(0.36 * 400 + 0.64 * 2500)
        self.assertAlmostEqual(summary["phonon_1d"]["hbar_omega_eff_meV"], omega_eff, delta=1e-6)
        self.assertAlmostEqual(summary["phonon_1d"]["delta_Q_amu05_A"], 1.0, delta=1e-9)
        self.assertAlmostEqual(summary["phonon_1d"]["S"], C * omega_eff, delta=1e-6)
        self.assert_view_data(omega_eff, omega_eff)
        self.assertEqual(summary["franck_condon"]["source"], "curves")
        self.assertIn("synthesized harmonic branches", summary["approximation"])
        pl = np.loadtxt(self.out / "pl_1d.tsv", skiprows=1)
        np.testing.assert_allclose(pl[:, 2], pl[:, 3])
        # Default ZPL without CCD points: the spin-purified one when present.
        self.assertAlmostEqual(summary["zpl_eV"]["used"], 3.75, places=9)

    def test_point_mode_two_minima_triplet_and_one_single_point(self):
        from ase import Atoms
        from ase.io import write

        def calc(name, energy, shift=0.0):
            directory = self.tmp / name
            directory.mkdir()
            write(directory / "CONTCAR", Atoms("C2", positions=[(1 + shift, 1, 1), (6, 1, 1)], cell=[10, 10, 10],
                                               pbc=True), format="vasp", direct=True)
            (directory / "OUTCAR").write_text(f"  free  energy   TOTEN  =     {energy:.8f} eV\n", encoding="ascii")
            return directory

        # Atom 0 moves 0.4 A; after centre-of-mass removal dQ = 0.4 sqrt(m_C / 2).
        dq = 0.4 * math.sqrt(12.011 / 2)
        relaxation = C * dq**2 * 30.0**2 / 1000
        args = ["--gs-dir", calc("gs", -100.0), "--es-dir", calc("es", -98.0, 0.4),
                "--triplet-dir", calc("t", -97.7, 0.4), "--gs-at-es-dir", calc("gs_es", -100.0 + relaxation, 0.4)]
        proc, payload = run(*args, "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        summary = self.summary()
        self.assertAlmostEqual(summary["delta_Q_amu05_A"], dq, delta=2e-3)
        self.assertAlmostEqual(summary["zpl_eV"]["from_ccd"], 2.0, places=6)
        self.assertAlmostEqual(summary["zpl_eV"]["spin_purified"], 1.7, places=6)
        self.assertAlmostEqual(payload["zpl_eV"], 1.7, places=6)
        for branch in ("gs", "es"):
            self.assertAlmostEqual(summary["harmonic"][branch]["hbar_omega_meV"], 30.0, delta=0.1)
        self.assertEqual(summary["harmonic"]["gs"]["curvature_from"], "gs_at_es_dir")
        self.assertIn("equal curvature", summary["harmonic"]["es"]["curvature_from"])
        self.assert_view_data(30.0, 30.0)
        self.assertEqual(summary["franck_condon"]["source"], "points")
        self.assertEqual(len(summary["points"]), 4)
        proc, payload = run(*args, "--zpl-formula", "E_gs@es - E_gs", "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        summary = self.summary()
        self.assertAlmostEqual(summary["zpl_eV"]["formula"], relaxation, places=7)
        self.assertAlmostEqual(payload["zpl_eV"], relaxation, places=7)
        self.assertEqual(summary["label"], "es")
        self.assertEqual(list(summary["zpl_formula"]["variables_eV"]), ["E_gs", "E_gs@es"])
        # Without any curvature source the run must refuse, not guess.
        proc, payload = run(*args[:6], "--out-dir", self.tmp / "out2")
        self.assertEqual(proc.returncode, 1)
        self.assertIn("curvature", payload["error"])

    def test_point_wells_win_over_phonons_and_export_all_five_points(self):
        from ase import Atoms
        from ase.io import write

        dq = 0.4 * math.sqrt(12.011 / 2)
        rg, re = (C * dq**2 * omega**2 / 1000 for omega in (30.0, 40.0))
        args = []
        for key, energy, shift in (("gs-dir", -100.0, 0), ("es-dir", -98.0, 0.4),
                                   ("triplet-dir", -97.7, 0.4), ("gs-at-es-dir", -100.0 + rg, 0.4),
                                   ("es-at-gs-dir", -98.0 + re, 0)):
            directory = self.tmp / key
            directory.mkdir()
            write(directory / "CONTCAR", Atoms("C2", positions=[(1 + shift, 1, 1), (6, 1, 1)],
                                               cell=[10, 10, 10], pbc=True), format="vasp", direct=True)
            (directory / "OUTCAR").write_text(f"  free  energy   TOTEN  =     {energy:.10f} eV\n", encoding="ascii")
            args.extend(("--" + key, directory))
        phonon = self.tmp / "phonon"
        phonon.mkdir()
        (phonon / "summary.json").write_text(json.dumps({"schema": "ds.phonon_vibronic.v1",
                                                         "zpl_eV": {"computed": 2.5}}), encoding="ascii")
        (phonon / "modes.tsv").write_text("freq_meV\tq_k_amu05_A\tis_translation\n50\t0.5\t0\n", encoding="ascii")
        proc, payload = run(*args, "--phonon-result", phonon, "--zpl-formula", "2*E_es - E_T - E_gs",
                            "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, (payload, proc.stderr[-3000:]))
        self.assert_view_data(30.0, 40.0)
        summary = self.summary()
        fc = summary["franck_condon"]
        self.assertAlmostEqual(fc["absorption_eV"], 1.7 + re, places=8)
        self.assertAlmostEqual(fc["emission_eV"], 1.7 - rg, places=8)
        self.assertAlmostEqual(fc["relaxation_gs_eV"], rg, places=8)
        self.assertAlmostEqual(fc["relaxation_es_eV"], re, places=8)
        with (self.out / "ccd_points.tsv").open(encoding="ascii") as handle:
            rows = list(csv.DictReader(handle, delimiter="\t"))
        self.assertEqual(len(rows), 5)
        self.assertTrue(all(row["status"] == "ok" for row in rows))
        expected = [("gs", 0, 0), ("es", 1, 1.7), ("gs", 1, rg),
                    ("es", 0, 1.7 + re), ("triplet", 1, 2.3)]
        for row, (branch, t, energy) in zip(rows, expected):
            self.assertEqual(row["branch"], branch)
            self.assertEqual(float(row["t"]), t)
            self.assertAlmostEqual(float(row["Q_amu05_A"]), t * dq, places=8)
            self.assertAlmostEqual(float(row["energy_eV"]), energy, places=8)
        self.assertEqual(summary["phonon_1d"]["hbar_omega_eff_meV"], 50)
        pl = np.loadtxt(self.out / "pl_1d.tsv", skiprows=1)
        self.assertGreater(np.max(np.abs(pl[:, 2] - pl[:, 3])), 0.1)
        # With no vertical points, the phonon frequency supplies the point-mode curves.
        proc, payload = run(*args[:6], "--phonon-result", phonon, "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, (payload, proc.stderr[-3000:]))
        self.assert_view_data(50.0, 50.0)
        self.assertAlmostEqual(self.summary()["delta_Q_amu05_A"], dq, places=8)
        self.assertEqual(self.summary()["franck_condon"]["source"], "curves")

    def test_path_without_vertical_endpoints_uses_curves_and_centres_q(self):
        make_ccd(self.ccd, gs_t=(-0.5, -0.25, 0, 0.25, 0.5, 0.75),
                 es_t=(0.25, 0.5, 0.75, 1, 1.25, 1.5))
        # An arbitrary origin on the input axis must disappear from all exported Q values.
        path = self.ccd / "ccd_path.csv"
        with path.open(encoding="ascii", newline="") as handle:
            rows = list(csv.DictReader(handle))
        for row in rows:
            row["Q_amu05_A"] = str(float(row["Q_amu05_A"]) + 4)
        with path.open("w", encoding="ascii", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=("branch", "t", "Q_amu05_A"))
            writer.writeheader()
            writer.writerows(rows)
        proc, payload = run("--ccd-dir", self.ccd, "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, (payload, proc.stderr[-3000:]))
        self.assert_view_data(30.0, 30.0)
        summary = self.summary()
        self.assertEqual(summary["franck_condon"]["source"], "curves")
        gs_min = next(p for p in summary["points"] if p["branch"] == "gs" and p["t"] == 0)
        self.assertEqual(gs_min["Q_amu05_A"], 0)
        curves = np.loadtxt(self.out / "ccd_curves.tsv", skiprows=1)
        self.assertAlmostEqual(curves[np.argmin(curves[:, 1]), 0], 0, delta=0.01)
        self.assertAlmostEqual(curves[np.argmin(curves[:, 2]), 0], 1, delta=0.01)

    @unittest.skipUnless((V2CBCN_DIMER / "ground_state" / "excited_geometry_HSE" / "OUTCAR").is_file(),
                         "V2CBCN dimer data in test-directory/ not present")
    def test_v2cbcn_dimer_point_mode_reference(self):
        proc, payload = run("--gs-dir", V2CBCN_DIMER / "ground_state" / "singlet_HSE",
                            "--es-dir", V2CBCN_DIMER / "exc_ms" / "singlet_HSE",
                            "--triplet-dir", V2CBCN_DIMER / "exc_ms" / "triplet_HSE",
                            "--gs-at-es-dir", V2CBCN_DIMER / "ground_state" / "excited_geometry_HSE",
                            "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        summary = self.summary()
        self.assertAlmostEqual(summary["delta_Q_amu05_A"], 0.3956, delta=1e-3)
        self.assertAlmostEqual(summary["zpl_eV"]["from_ccd"], 3.7551, delta=1e-3)
        self.assertAlmostEqual(summary["zpl_eV"]["used"], 4.2046, delta=1e-3)
        self.assertAlmostEqual(summary["harmonic"]["gs"]["hbar_omega_meV"], 110.16, delta=0.5)
        self.assertAlmostEqual(summary["harmonic"]["gs"]["S"], 2.063, delta=0.02)
        proc, payload = run("--gs-dir", V2CBCN_DIMER / "ground_state" / "singlet_HSE",
                            "--es-dir", V2CBCN_DIMER / "exc_ms" / "singlet_HSE",
                            "--triplet-dir", V2CBCN_DIMER / "exc_ms" / "triplet_HSE",
                            "--gs-at-es-dir", V2CBCN_DIMER / "ground_state" / "excited_geometry_HSE",
                            "--zpl-formula", "2*E_es - E_T - E_gs", "--zpl", 1,
                            "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        formula_summary = self.summary()
        self.assertAlmostEqual(formula_summary["zpl_eV"]["formula"], summary["zpl_eV"]["spin_purified"])
        self.assertAlmostEqual(payload["zpl_eV"], 4.2046, delta=1e-3)
        self.assertEqual(list(formula_summary["zpl_formula"]["variables_eV"]), ["E_gs", "E_es", "E_T"])
        omega = formula_summary["harmonic"]["gs"]["hbar_omega_meV"]
        self.assert_view_data(omega, omega)
        self.assertEqual(formula_summary["franck_condon"]["source"], "points")
        # The reference dataset has no es-at-gs calculation: only four real points exist.
        self.assertEqual(len(formula_summary["points"]), 4)

    @unittest.skipUnless((VV / "ccd_path.csv").is_file(), "VV CCD data in test-directory/ not present")
    def test_vv_reader_flags_bad_points(self):
        proc, payload = run("--ccd-dir", VV, "--out-dir", self.out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        points = {(p["branch"], round(p["t"], 4)): p["status"] for p in self.summary()["points"]}
        self.assertEqual(points[("es", 1.5)], "unconverged")
        self.assertEqual(points[("es", 1.75)], "missing")
        self.assertEqual(sum(status == "ok" for status in points.values()), 16)


if __name__ == "__main__":
    unittest.main()
