"""Task 104e: scripts/python/examples/ccd_inputs.py (generalised sjcm_mgr build_ccd.py)."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts" / "python" / "examples" / "ccd_inputs.py"
CASE = ROOT / "test-directory" / "sjcm_mgr" / "PL-spectra" / "dimer" / "CH0" / "R2SCAN" / "9x9x1"


def run(*args):
    proc = subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], capture_output=True, text=True,
                          encoding="utf-8", timeout=600)
    lines = [line for line in proc.stdout.splitlines() if line.strip()]
    return proc, lines


class CcdInputsTests(unittest.TestCase):
    def test_self_test(self):
        proc, lines = run("--self-test")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(lines[-1], "demo: OK")

    def test_missing_arguments_report_json_error(self):
        proc, lines = run("--gs-dir", "x")
        self.assertEqual(proc.returncode, 1)
        self.assertFalse(json.loads(lines[-1])["ok"])

    @unittest.skipUnless((CASE / "relax").is_dir(), "reference data in test-directory/ not present")
    def test_dimer_ccd_points(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "ccd"
            proc, lines = run("--gs-dir", CASE / "relax", "--es-dir", CASE / "excited" / "try_1_singlet",
                              "--out-dir", out)
            self.assertEqual(proc.returncode, 0, proc.stderr[-2000:])
            payload = json.loads(lines[-1])
            self.assertTrue(payload["ok"])
            self.assertEqual(payload["points"], 18)
            # same dQ as phonon_vibronic_analysis.py's delta_Q_amu05_A for this case
            self.assertAlmostEqual(payload["delta_Q_amu05_A"], 0.3344, delta=0.001)
            self.assertEqual(len(list((out / "es").glob("t_*"))), 8)
            self.assertEqual(len(list((out / "gs").glob("t_*"))), 10)
            incar = (out / "es" / "t_+050" / "INCAR").read_text(encoding="ascii")
            self.assertIn("NSW = 0", incar)
            self.assertIn("ISTART = 1", incar)
            self.assertNotIn(b"\r", (out / "gs" / "run.job").read_bytes())


if __name__ == "__main__":
    unittest.main()
