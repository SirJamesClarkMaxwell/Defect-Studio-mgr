"""Mode-major dephonopy fixture, including translations and unequal atomic masses."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch
from uuid import uuid4

import h5py
import numpy as np
from ase import Atoms
from ase.io import write

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/python/examples"))
from phonon_mode_vector import analyse, write_result
from phonon_vibronic_analysis import publish


class ModeVectorTests(unittest.TestCase):
    def setUp(self):
        self.root = ROOT / ".task-113g-temp" / uuid4().hex
        self.embedded = self.root / "server/embedded/2x2x1"
        self.embedded.mkdir(parents=True)
        self.addCleanup(shutil.rmtree, self.root)
        self.atoms = Atoms("BN", positions=[[0, 0, 0], [1, 1, 1]], cell=[5, 5, 5], pbc=True)
        write(self.embedded / "POSCAR", self.atoms, format="vasp")
        self.rows = np.arange(1, 37, dtype=float).reshape(6, 6)
        self.frequencies = np.array([0., -1.e-5, 1.e-5, -5., 100., 200.])
        with h5py.File(self.embedded / "modes.h5", "w") as handle:
            handle["evecs"] = self.rows
            handle["freq_mev"] = self.frequencies
            handle["freq"] = self.frequencies / 27210.68427103
            handle["evals"] = np.arange(6.)
            handle["cell"] = np.asarray(self.atoms.cell)
            handle["scaled_positions"] = self.atoms.get_scaled_positions()
            handle["atomic_symbols"] = np.asarray(self.atoms.get_chemical_symbols(), dtype="S4")

    def test_slice_equals_full_local_conversion_and_participation_sums_to_one(self):
        with h5py.File(self.embedded / "modes.h5", "r") as handle:
            full = handle["evecs"][:].reshape(6, 2, 3)
        physical = full / np.sqrt(self.atoms.get_masses())[None, :, None]
        physical /= np.linalg.norm(physical, axis=(1, 2))[:, None, None]
        expected = physical * np.sqrt(self.atoms.get_masses())[None, :, None]
        expected /= np.linalg.norm(expected, axis=(1, 2))[:, None, None]
        for index in range(6):
            result = analyse(self.embedded, index)
            np.testing.assert_allclose(result["eigenvector"], expected[index], rtol=1e-6)
            self.assertAlmostEqual(float(np.sum(result["eigenvector"]**2)), 1., places=6)
            self.assertEqual(result["summary"]["mode_index"], index)
            self.assertEqual(result["summary"]["frequency_meV"], self.frequencies[index])

    def test_only_one_row_and_one_frequency_are_read_with_no_translation_offset(self):
        calls = []
        original = h5py.Dataset.__getitem__

        def record(dataset, key, *args, **kwargs):
            calls.append((dataset.name, key))
            return original(dataset, key, *args, **kwargs)

        with patch.object(h5py.Dataset, "__getitem__", record):
            result = analyse(self.embedded, 3)
        self.assertEqual(calls, [("/evecs", (3, slice(None))), ("/freq_mev", 3)])
        self.assertEqual(result["summary"]["frequency_meV"], -5.)

    def test_publish_float32_and_failure_preserves_previous_output(self):
        output = self.root / "output"
        publish(analyse(self.embedded, 5), output, writer=write_result)
        self.assertEqual(json.loads((output / "mode.json").read_text())["schema"], "ds.phonon_mode_vector.v1")
        array = np.load(output / "eigenvector.npy")
        self.assertEqual(array.shape, (2, 3))
        self.assertEqual(array.dtype, np.dtype("<f4"))

        def fail(result, directory):
            raise RuntimeError("injected write failure")

        with self.assertRaises(RuntimeError):
            publish(analyse(self.embedded, 4), output, writer=fail)
        np.testing.assert_array_equal(np.load(output / "eigenvector.npy"), array)

    def test_bad_index_shapes_nonfinite_and_zero_norm(self):
        for index in (-1, 6, True):
            with self.assertRaises(ValueError):
                analyse(self.embedded, index)
        for value in (float("nan"), float("inf"), 0.):
            with h5py.File(self.embedded / "modes.h5", "a") as handle:
                handle["evecs"][3, :] = value
            with self.assertRaises(ValueError):
                analyse(self.embedded, 3)
        with h5py.File(self.embedded / "modes.h5", "a") as handle:
            del handle["evecs"]
            handle["evecs"] = np.zeros((5, 6))
        with self.assertRaisesRegex(ValueError, "shapes"):
            analyse(self.embedded, 3)

    def test_cli_missing_input_and_server_overlap_are_json_errors(self):
        for embedded, output in ((self.embedded, self.embedded / "output"),
                                 (self.root / "missing/embedded/2x2x1", self.root / "output")):
            process = subprocess.run([sys.executable, str(ROOT / "scripts/python/examples/phonon_mode_vector.py"),
                                      "--embedded-dir", str(embedded), "--mode", "3", "--out-dir", str(output)],
                                     capture_output=True, text=True)
            self.assertEqual(process.returncode, 1)
            self.assertFalse(json.loads(process.stdout)["ok"])
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
