"""Regression tests for the puntukas 0.0.1 bridge API migration."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest

import numpy as np


REPO_ROOT = Path(__file__).resolve().parents[3]
EXAMPLES_DIR = REPO_ROOT / "scripts" / "python" / "examples"
FIXTURES_DIR = REPO_ROOT / "tests" / "ScientificRuntime" / "fixtures"


def load_example(name: str):
    path = EXAMPLES_DIR / f"{name}.py"
    spec = importlib.util.spec_from_file_location(f"test_{name}", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class PuntukasMigrationTests(unittest.TestCase):
    def test_structure_loader_uses_new_atoms_file_api_in_angstrom(self):
        module = load_example("puntukas_structure_load")

        self.assertTrue(hasattr(module, "Atoms"))
        payload = module.load_structure_payload(str(FIXTURES_DIR / "POSCAR_Si.vasp"))

        self.assertEqual(payload["reduced_formula"], "Si2")
        self.assertAlmostEqual(payload["lattice"][0][0], 5.43)
        self.assertAlmostEqual(payload["sites"][1]["cartesian"][0], 1.3575)

    def test_orbital_loader_uses_orbital_info_and_ireps_spelling(self):
        module = load_example("vasp_output_load")
        rows = np.array(
            [(0, -1.0, 1.0, 2.0, "A1", -0.5, 0.0, 3.0, "B2")],
            dtype=[
                ("nr", "i4"),
                ("e(up)", "f4"),
                ("occ(up)", "f4"),
                ("loc(up)", "f4"),
                ("irrep(up)", "U6"),
                ("e(down)", "f4"),
                ("occ(down)", "f4"),
                ("loc(down)", "f4"),
                ("irrep(down)", "U6"),
            ],
        )

        class Output:
            def __init__(self):
                self.call = None

            def orbital_info(self, start, stop, **kwargs):
                self.call = (start, stop, kwargs)
                return rows

        output = Output()
        records, error = module._orbitals_payload(output, 1, 1, True, 0.2, 0.01)

        self.assertIsNone(error)
        self.assertEqual(output.call, (0, 1, {"ireps": True, "irep_tol": 0.2, "symprec": 0.01}))
        self.assertEqual(records[0]["band"], 1)
        self.assertEqual(records[0]["up"]["irrep"], "A1")

    def test_orbital_loader_preserves_single_channel_bridge_contract(self):
        module = load_example("vasp_output_load")
        rows = np.array(
            [(4, -0.25, 1.0, 1.5)],
            dtype=[("nr", "i4"), ("energy", "f4"), ("occ", "f4"), ("loc", "f4")],
        )

        class Output:
            @staticmethod
            def orbital_info(*_args, **_kwargs):
                return rows

        records, error = module._orbitals_payload(Output(), 5, 5)

        self.assertIsNone(error)
        self.assertEqual(records[0]["band"], 5)
        self.assertEqual(records[0]["up"]["occupation"], 1.0)
        self.assertEqual(records[0]["down"]["energy"], records[0]["up"]["energy"])
        self.assertEqual(records[0]["down"]["occupation"], 0.0)

    def test_grid_loader_uses_wavefunction_and_to_real_space(self):
        module = load_example("vasp_orbital_grid_load")

        class Cell:
            array = np.eye(3) * 5.0

        class RealSpace:
            data = np.arange(8, dtype=np.complex128).reshape(2, 2, 2)
            cell = Cell()

        class Field:
            meta = {"energy": -2.5, "occupation": 1.0}

            def to_real_space(self):
                return RealSpace()

        class Wavecar:
            def __init__(self):
                self.call = None

            def wavefunction(self, spin, kpoint, band):
                self.call = (spin, kpoint, band)
                return Field()

        wavecar = Wavecar()

        class Output:
            pass

        output = Output()
        output.wavecar = wavecar
        module.VaspOutput.from_directory = staticmethod(lambda _directory: output)

        payload = module.load_orbital_grid_payload(".", 1, 2, 3)
        try:
            self.assertEqual(wavecar.call, (1, 2, 2))
            self.assertEqual(payload["dims"], [2, 2, 2])
            self.assertEqual(payload["cell"], (np.eye(3) * 5.0).tolist())
            self.assertEqual(payload["energy"], -2.5)
            self.assertEqual(payload["occupation"], 1.0)
        finally:
            Path(payload["gridPath"]).unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
