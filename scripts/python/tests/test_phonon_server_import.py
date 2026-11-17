"""Small server tree: real readers/spectra/publisher, no force constants or eigenvectors."""
import json
import math
import shutil
import subprocess
import sys
import unittest
from pathlib import Path
from uuid import uuid4

import numpy as np
from ase import Atoms
from ase.io import write

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/python/examples"))
from phonon_server_import import analyse, map_cluster_path
from phonon_vibronic_analysis import mode_factors, publish


class ServerImportTests(unittest.TestCase):
    def setUp(self):
        self.root = ROOT / ".task-113a-temp" / ("server-" + uuid4().hex)
        self.root.mkdir(parents=True)
        self.addCleanup(shutil.rmtree, self.root)
        self.system = self.root / "mount/PL-spectra/dimer/1x1x1"
        self.hr = self.system / "spectral-function/2x2x1/displacements_cutoff4A"
        self.hr.mkdir(parents=True)
        self.embedded = self.system / "embedded/2x2x1"
        self.embedded.mkdir(parents=True)
        self.atoms = Atoms("CC", positions=[[1, 1, 1], [2, 1, 1]], cell=[5, 5, 5], pbc=True)
        write(self.embedded / "POSCAR", self.atoms, format="vasp")
        for state, energy, displacements in (("relax", -12.0, [0, 0]), ("excited", -9.5, [.05, -.05])):
            directory = self.system / state
            directory.mkdir()
            atoms = self.atoms.copy()
            atoms.positions[:, 0] += displacements
            write(directory / "CONTCAR_aligned", atoms, format="vasp")
            (directory / "OUTCAR").write_text(f"free energy TOTEN = -20 eV\nfree energy TOTEN = {energy} eV\n")
        (self.hr / "hr_factors.log").write_text(
            "init_path: /scratch/user/sjcm_mgr/PL-spectra/dimer/1x1x1/excited/CONTCAR_aligned\n"
            "final_path: /scratch/user/sjcm_mgr/PL-spectra/dimer/1x1x1/relax/CONTCAR_aligned\n"
            "cutoff radius (user): 4 A\nStot: 999\n")
        for name, values in (("freq_mev", [-5, 100, 200]), ("hr_factors", [.9, .25, .75]),
                             ("fc_displ", [1, -1, 1]), ("irreps", ["???", "A1", "B2"])):
            (self.hr / f"{name}.dat").write_text("\n".join(map(str, values)) + "\n")

    def result(self, **kwargs):
        return analyse(self.hr, sigma=2.0, gamma=.5, **kwargs)

    def test_sticks_zpl_displacement_and_publish_without_npy(self):
        result = self.result(zpl_formula="E_es - E_gs", zpl_exp=2.6)
        summary = result["summary"]
        self.assertEqual(summary["s_total"], 1.0)
        self.assertEqual(summary["debye_waller"], math.exp(-1))
        self.assertEqual(summary["zpl_eV"]["computed"], 2.5)
        self.assertEqual(summary["zpl_eV"]["formula"], 2.5)
        self.assertAlmostEqual(summary["max_displacement_A"], .05, places=8)
        self.assertAlmostEqual(summary["delta_Q_amu05_A"], math.sqrt(2 * self.atoms.get_masses()[0] * .05**2))
        self.assertEqual(summary["embedding"], {"mult": [2, 2, 1]})
        self.assertEqual(summary["hr_method"], "displacements")
        self.assertEqual(summary["hr_cutoff_A"], 4)
        self.assertFalse(summary["stable"])
        self.assertEqual(summary["imaginary_modes"][0]["index"], 3)
        self.assertEqual(result["is_translation"].tolist(), [True, True, True, False, False, False])
        self.assertTrue(np.isnan(result["n_eff"]).all())
        self.assertLess(result["q"][4], 0)
        self.assertGreater(result["q"][5], 0)
        # Invert the same SI formula as local mode_factors, independently of server displacement units.
        for index in (4, 5):
            _, _, factors = mode_factors(np.array([0, 0, 0, result["frequencies"][index]]),
                                         np.ones((4, 1, 1)), np.ones(1), np.array([[result["q"][index]]]))
            self.assertAlmostEqual(factors[-1], result["s_k"][index])
        out = publish(result, self.root / "output")
        self.assertFalse((out / "eigenvectors.npy").exists())
        self.assertFalse((out / "participation.npy").exists())
        self.assertTrue((out / "POSCAR").exists())
        self.assertIn("nan", (out / "modes.tsv").read_text())
        self.assertFalse(json.loads((out / "summary.json").read_text())["has_eigenvectors"])
        self.assertAlmostEqual(np.trapezoid(result["spectral_function"][:, 1], result["spectral_function"][:, 0]), 1)
        for column in (1, 3, 4):
            self.assertAlmostEqual(result["pl"][:, column].max(), 1)

    def test_unmapped_states_still_publish_spectra_without_pl(self):
        (self.hr / "hr_factors.log").write_text("init_path: /unmapped/a\nfinal_path: /unmapped/b\n")
        result = self.result()
        self.assertIsNone(result["summary"]["zpl_eV"]["computed"])
        self.assertIsNone(result["summary"]["delta_Q_amu05_A"])
        self.assertIsNone(result["summary"]["max_displacement_A"])
        self.assertIsNone(result["summary"]["input_paths"]["gs_dir"])
        self.assertEqual(result["pl"].shape, (0, 5))
        publish(result, self.root / "unknown")
        with self.assertRaisesRegex(ValueError, "E_es"):
            self.result(zpl_formula="E_es-E_gs")
        self.assertEqual(self.result(zpl_formula="1.945")["summary"]["zpl_eV"]["formula"], 1.945)

    def test_embedded_iprs_and_frequency_header_units(self):
        np.savetxt(self.embedded / "iprs.dat", [2, 2, 2, 1.5, 1.2, 1.1])
        np.savetxt(self.embedded / "freq.dat", [-.1, .2, .3, 1, 2, 3], header="frequencies (mev)")
        result = self.result()
        np.testing.assert_array_equal(result["frequencies"][:3], [-.1, .2, .3])
        np.testing.assert_array_equal(result["n_eff"], [2, 2, 2, 1.5, 1.2, 1.1])
        np.savetxt(self.embedded / "freq.dat", [1e-7] * 6, header="frequencies (au)")
        self.assertAlmostEqual(self.result()["frequencies"][0], .002721068427103)

    def test_fallback_structure_and_missing_outcar(self):
        dispersion = self.system / "dispersion"
        dispersion.mkdir()
        (self.embedded / "POSCAR").replace(dispersion / "SPOSCAR")
        (self.system / "relax/OUTCAR").unlink()
        result = self.result()
        self.assertIsNone(result["summary"]["zpl_eV"]["computed"])
        self.assertIsNotNone(result["summary"]["delta_Q_amu05_A"])

    def test_longest_suffix_beats_a_nearer_short_match(self):
        (self.hr / "relax").mkdir()
        (self.hr / "relax/CONTCAR_aligned").write_text("wrong")
        mapped = map_cluster_path("/scratch/user/PL-spectra/dimer/1x1x1/relax/CONTCAR_aligned", self.hr)
        self.assertEqual(mapped, (self.system / "relax/CONTCAR_aligned").resolve())

    def test_validation_and_unavailable_formula_inputs(self):
        for kwargs in ({"sigma": 0}, {"gamma": float("nan")}, {"zpl_exp": -1}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                analyse(self.hr, **({"sigma": 2, "gamma": .5} | kwargs))
        with self.assertRaisesRegex(ValueError, "E_T"):
            self.result(zpl_formula="2*E_es-E_T-E_gs")
        (self.hr / "hr_factors.dat").write_text("1\n")
        with self.assertRaisesRegex(ValueError, "3 rows"):
            self.result()

    def test_cli_reimport_is_atomic_and_never_writes_server_tree(self):
        before = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.system.rglob("*") if p.is_file()}
        out = self.root / "cli-output"
        command = [sys.executable, str(ROOT / "scripts/python/examples/phonon_server_import.py"),
                   "--hr-dir", str(self.hr), "--sigma", "2", "--gamma", ".5", "--out-dir", str(out)]
        for _ in range(2):
            proc = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
            self.assertTrue(json.loads(proc.stdout.splitlines()[-1])["ok"])
        after = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.system.rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        command[-1] = str(self.system / "derived")
        proc = subprocess.run(command, capture_output=True, text=True, timeout=60)
        self.assertNotEqual(proc.returncode, 0)
        self.assertFalse((self.system / "derived").exists())
