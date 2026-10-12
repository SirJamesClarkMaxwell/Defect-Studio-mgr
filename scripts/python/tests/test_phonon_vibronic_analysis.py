"""Task 104a: scripts/python/examples/phonon_vibronic_analysis.py.

Reference numbers (C2 dimer in hBN, 9x9x1, r2SCAN) were computed independently of the script:
S_k = omega_k q_k^2 / (2 hbar) from the renormalised dephonopy eigenvectors, which agrees with
dephonopy's hr_factors_from_displacements (1.40708 vs 1.40712). ZPLs are TOTEN differences of the
three OUTCARs. The reference data lives in the gitignored test-directory/ - those tests skip
without it. See docs/work/project/tasks/104-phonon-vibronic-script.md.
"""

import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts" / "python" / "examples" / "phonon_vibronic_analysis.py"
CASE = ROOT / "test-directory" / "sjcm_mgr" / "PL-spectra" / "dimer" / "CH0" / "R2SCAN" / "9x9x1"

try:
    import numpy as np
    import dephonopy  # noqa: F401
except ImportError as exc:
    IMPORT_ERROR = exc
else:
    IMPORT_ERROR = None


def run_script(*args):
    proc = subprocess.run(
        [sys.executable, str(SCRIPT), *map(str, args)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=600,
    )
    lines = [line for line in proc.stdout.splitlines() if line.strip()]
    payload = json.loads(lines[-1]) if lines else None
    return proc, payload


def read_tsv(path):
    with open(path, encoding="utf-8") as handle:
        header = handle.readline().split()
        rows = [line.split() for line in handle if line.strip()]
    return header, rows


@unittest.skipIf(IMPORT_ERROR is not None, f"dephonopy/numpy missing: {IMPORT_ERROR}")
class FailureTests(unittest.TestCase):
    def test_bad_input_reports_json_error_and_leaves_no_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "result"
            proc, payload = run_script(
                "--fc-dir", Path(tmp) / "missing",
                "--gs-dir", Path(tmp) / "missing",
                "--es-dir", Path(tmp) / "missing",
                "--out-dir", out,
            )
            self.assertNotEqual(proc.returncode, 0)
            self.assertIsNotNone(payload, proc.stderr[-2000:])
            self.assertFalse(payload["ok"])
            self.assertTrue(payload["error"])
            self.assertFalse(out.exists())


@unittest.skipIf(IMPORT_ERROR is not None, f"dephonopy/numpy missing: {IMPORT_ERROR}")
@unittest.skipUnless((CASE / "dispersion").is_dir(), "reference data in test-directory/ not present")
class ReferenceCaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = Path(cls.tmp.name) / "result"
        cls.proc, cls.payload = run_script(
            "--fc-dir", CASE / "dispersion",
            "--gs-dir", CASE / "relax",
            "--es-dir", CASE / "excited" / "try_1_singlet",
            "--triplet-dir", CASE / "excited" / "try_1_tryplet",
            "--zpl-exp", 4.08,
            "--out-dir", cls.out,
        )

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.assertEqual(self.proc.returncode, 0, self.proc.stderr[-3000:])
        self.assertTrue(self.payload["ok"])

    def summary(self):
        return json.loads((self.out / "summary.json").read_text(encoding="utf-8"))

    def test_stdout_json_matches_summary(self):
        summary = self.summary()
        self.assertEqual(summary["schema"], "ds.phonon_vibronic.v1")
        self.assertAlmostEqual(self.payload["s_total"], summary["s_total"], places=9)
        self.assertEqual(Path(self.payload["out_dir"]).resolve(), self.out.resolve())

    def test_sizes_and_translations(self):
        summary = self.summary()
        self.assertEqual(summary["n_atoms"], 162)
        self.assertEqual(summary["n_modes"], 486)
        self.assertEqual(len(summary["translations"]["indices"]), 3)
        for freq in summary["translations"]["freq_meV"]:
            self.assertLess(abs(freq), 1.0)

    def test_huang_rhys_and_debye_waller(self):
        summary = self.summary()
        self.assertAlmostEqual(summary["s_total"], 1.407, delta=0.01)
        self.assertAlmostEqual(summary["debye_waller"], math.exp(-summary["s_total"]), places=9)

    def test_zpl_values(self):
        zpl = self.summary()["zpl_eV"]
        self.assertAlmostEqual(zpl["computed"], 3.452, delta=0.002)
        self.assertAlmostEqual(zpl["computed_spin_purified"], 3.749, delta=0.002)
        self.assertAlmostEqual(zpl["experimental"], 4.08, places=6)

    def test_modes_table(self):
        header, rows = read_tsv(self.out / "modes.tsv")
        self.assertEqual(
            header,
            ["index", "freq_meV", "irrep", "n_eff", "beta", "S_k", "q_k_amu05_A", "is_translation"],
        )
        self.assertEqual(len(rows), 486)
        col = {name: i for i, name in enumerate(header)}
        s_k = [float(r[col["S_k"]]) for r in rows]
        top = max(range(len(rows)), key=lambda i: s_k[i])
        self.assertAlmostEqual(float(rows[top][col["freq_meV"]]), 200.1, delta=0.2)
        self.assertEqual(rows[top][col["irrep"]], "A1")
        self.assertAlmostEqual(s_k[top], 0.525, delta=0.01)
        self.assertAlmostEqual(float(rows[top][col["beta"]]), 35.5, delta=0.5)
        self.assertAlmostEqual(sum(s_k), self.summary()["s_total"], delta=1e-6)
        for r in rows:
            if r[col["is_translation"]] in ("1", "true", "True"):
                self.assertEqual(float(r[col["S_k"]]), 0.0)

    def test_participation_and_eigenvectors(self):
        part = np.load(self.out / "participation.npy")
        vecs = np.load(self.out / "eigenvectors.npy")
        self.assertEqual(part.shape, (486, 162))
        self.assertEqual(vecs.shape, (486, 162, 3))
        np.testing.assert_allclose(part.sum(axis=1), 1.0, atol=1e-4)
        np.testing.assert_allclose((vecs.astype(np.float64) ** 2).sum(axis=2), part, atol=1e-4)

    def test_structure(self):
        structure = json.loads((self.out / "structure.json").read_text(encoding="utf-8"))
        self.assertEqual(len(structure["symbols"]), 162)
        self.assertEqual(structure["symbols"].count("C"), 2)
        self.assertEqual(np.asarray(structure["positions_A"]).shape, (162, 3))
        self.assertEqual(np.asarray(structure["cell_A"]).shape, (3, 3))

    def test_spectral_function_integrates_to_s_total(self):
        header, rows = read_tsv(self.out / "spectral_function.tsv")
        self.assertEqual(header, ["energy_meV", "S_E_per_meV"])
        data = np.array(rows, dtype=float)
        integral = np.trapezoid(data[:, 1], data[:, 0])
        self.assertAlmostEqual(integral, self.summary()["s_total"], delta=0.01 * self.summary()["s_total"])

    def test_pl_lineshape_sideband_below_zpl(self):
        header, rows = read_tsv(self.out / "pl.tsv")
        self.assertEqual(
            header,
            ["photon_energy_eV", "intensity_computed", "intensity_spin_purified", "intensity_experimental"],
        )
        data = np.array(rows, dtype=float)
        energy = data[:, 0]
        zpl = self.summary()["zpl_eV"]
        for column, key in ((1, "computed"), (2, "computed_spin_purified"), (3, "experimental")):
            intensity = data[:, column]
            self.assertAlmostEqual(np.nanmax(intensity), 1.0, places=6)
            # T = 0 emission: almost no weight above the ZPL beyond the Lorentzian tail
            above = np.trapezoid(np.where(energy > zpl[key] + 0.05, intensity, 0.0), energy)
            total = np.trapezoid(intensity, energy)
            self.assertLess(above / total, 0.02, key)


if __name__ == "__main__":
    unittest.main()
