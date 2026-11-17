"""Task 104a: scripts/python/examples/phonon_vibronic_analysis.py.

Reference numbers (C2 dimer in hBN, 9x9x1, r2SCAN) were computed independently of the script:
S_k = omega_k q_k^2 / (2 hbar) from the renormalised dephonopy eigenvectors, which agrees with
dephonopy's hr_factors_from_displacements (1.40708 vs 1.40712). ZPLs are TOTEN differences of the
three OUTCARs. The reference data lives in the gitignored test-directory/ - those tests skip
without it. See docs/work/project/tasks/104-phonon-vibronic-script.md.
"""

import json
from contextlib import contextmanager
import math
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts" / "python" / "examples" / "phonon_vibronic_analysis.py"
sys.path.insert(0, str(SCRIPT.parent))
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


@contextmanager
def temporary_directory():
    # mkdir inherits the repo ACL; Python 3.13 mkdtemp's private ACL blocks the sandbox.
    directory = ROOT / ".task-113b-temp" / uuid4().hex
    directory.mkdir(parents=True)
    try:
        yield directory
    finally:
        shutil.rmtree(directory)


@unittest.skipIf(IMPORT_ERROR is not None, f"dephonopy/numpy missing: {IMPORT_ERROR}")
class HrControlsTests(unittest.TestCase):
    def test_invalid_method_inputs_fail_before_reading_files(self):
        from phonon_vibronic_analysis import analyse

        for options, message in (({"hr_method": "unknown"}, "hr-method"),
                                 ({"hr_method": "approximate_forces"}, "bulk-fc-dir"),
                                 ({"hr_method": "forces"}, "gs-at-es-dir"),
                                 ({"hr_cutoff": 0}, "hr-cutoff"),
                                 ({"hr_cutoff": float("nan")}, "hr-cutoff"),
                                 ({"sigma_max": 5}, "sigma-max"),
                                 ({"sigma_max": float("inf")}, "sigma-max")):
            with self.subTest(options=options), self.assertRaisesRegex(ValueError, message):
                analyse("missing", "missing", "missing", **options)

    def test_sigma_max_endpoint_widths_and_monotonic_broadening(self):
        from phonon_vibronic_analysis import build_spectra

        widths = []
        for sigma_max in (2, 4, 8):
            spectral, _ = build_spectra(np.array([100., 200.]), np.array([0., 1.]),
                                        np.zeros(2, dtype=bool), 1., {}, 2., .5, sigma_max)
            energy, density = spectral.T
            widths.append(np.sqrt(np.trapezoid((energy - 200)**2 * density, energy)))
            self.assertAlmostEqual(widths[-1], sigma_max, places=6)
            self.assertAlmostEqual(np.trapezoid(density, energy), 1.)
        self.assertTrue(widths[0] < widths[1] < widths[2])
        spectral, _ = build_spectra(np.array([100., 200.]), np.array([1., 0.]),
                                    np.zeros(2, dtype=bool), 1., {}, 2., .5, 8.)
        self.assertAlmostEqual(np.sqrt(np.trapezoid((spectral[:, 0] - 100)**2 * spectral[:, 1],
                                                   spectral[:, 0])), 2., places=6)

    def test_sigma_max_handles_one_mode_and_equal_frequencies(self):
        from phonon_vibronic_analysis import build_spectra

        for frequencies in (np.array([100.]), np.array([100., 100.])):
            spectral, _ = build_spectra(frequencies, np.ones(len(frequencies)),
                                        np.zeros(len(frequencies), dtype=bool), float(len(frequencies)),
                                        {}, 2., .5, 8.)
            self.assertTrue(np.isfinite(spectral).all())
            self.assertAlmostEqual(np.trapezoid(spectral[:, 1], spectral[:, 0]), len(frequencies))


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.gettempdir()) / f"ds-publish-{uuid4().hex}"
        self.tmp.mkdir()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.input = self.tmp / "input"
        self.input.mkdir()
        self.source = self.input / "OUTCAR"
        self.source.write_text("keep", encoding="utf-8")
        self.result = {"summary": {"input_paths": {"fc_dir": str(self.input), "triplet_dir": None}}}

    @staticmethod
    def write(result, directory):
        (directory / "summary.json").write_text(json.dumps(result["summary"]), encoding="utf-8")

    def test_publish_inside_input_and_republish_relative_to_final_directory(self):
        from phonon_vibronic_analysis import publish

        out = self.input / "defectstudio" / "dispersion"
        publish(self.result, out, writer=self.write)
        summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["inputs_relative"], {"fc_dir": "../..", "triplet_dir": None})
        self.assertEqual(summary["input_paths"], self.result["summary"]["input_paths"])
        other = self.tmp / "elsewhere"
        publish(self.result, other, writer=self.write)
        self.assertEqual(self.result["summary"]["inputs_relative"]["fc_dir"], "../input")
        publish(self.result, out, writer=self.write)
        self.assertEqual(self.source.read_text(encoding="utf-8"), "keep")

    def test_rejects_output_that_would_delete_input(self):
        from phonon_vibronic_analysis import publish

        for out in (self.input, self.tmp):
            with self.subTest(out=out), self.assertRaises(ValueError):
                publish(self.result, out, writer=self.write)
        self.assertEqual(self.source.read_text(encoding="utf-8"), "keep")

    def test_other_drive_keeps_absolute_path(self):
        from phonon_vibronic_analysis import publish

        with patch.object(os.path, "relpath", side_effect=ValueError("different drives")):
            publish(self.result, self.tmp / "out", writer=self.write)
        self.assertEqual(self.result["summary"]["inputs_relative"]["fc_dir"], str(self.input).replace("\\", "/"))

    def test_failed_writer_preserves_previous_result_and_inputs(self):
        from phonon_vibronic_analysis import publish

        out = self.input / "defectstudio" / "dispersion"
        publish(self.result, out, writer=self.write)
        previous = (out / "summary.json").read_bytes()
        def fail(result, directory):
            raise OSError("writer failed")
        with self.assertRaises(OSError):
            publish(self.result, out, writer=fail)
        self.assertEqual((out / "summary.json").read_bytes(), previous)
        self.assertEqual(self.source.read_text(encoding="utf-8"), "keep")


@unittest.skipIf(IMPORT_ERROR is not None, f"dephonopy/numpy missing: {IMPORT_ERROR}")
class FailureTests(unittest.TestCase):
    def test_bad_input_reports_json_error_and_leaves_no_output(self):
        with temporary_directory() as tmp:
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
        cls.tmp = cls.enterClassContext(temporary_directory())
        cls.out = cls.tmp / "result"
        cls.unused = cls.tmp / "unused-without-outcar"
        cls.unused.mkdir()
        cls.proc, cls.payload = run_script(
            "--fc-dir", CASE / "dispersion",
            "--gs-dir", CASE / "relax",
            "--es-dir", CASE / "excited" / "try_1_singlet",
            "--triplet-dir", CASE / "excited" / "try_1_tryplet",
            "--zpl-exp", 4.08,
            "--zpl-formula", "2*E_es - E_T - E_gs",
            "--label", "Dimer α",
            "--gs-at-es-dir", cls.unused,
            "--out-dir", cls.out,
        )

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
        self.assertAlmostEqual(zpl["formula"], zpl["computed_spin_purified"], places=9)
        summary = self.summary()
        self.assertEqual(summary["label"], "Dimer α")
        self.assertEqual(summary["zpl_formula"]["text"], "2*E_es - E_T - E_gs")
        self.assertEqual(list(summary["zpl_formula"]["variables_eV"]), ["E_gs", "E_es", "E_T"])
        self.assertEqual(summary["zpl_formula"]["value_eV"], zpl["formula"])
        self.assertEqual(summary["input_paths"]["gs_at_es_dir"], str(self.unused.resolve()))
        for key, path in summary["input_paths"].items():
            if path:
                self.assertEqual((self.out / summary["inputs_relative"][key]).resolve(), Path(path))

    def test_cross_geometry_formula_reads_only_toten(self):
        cross = self.tmp / "es_at_gs"
        cross.mkdir(exist_ok=True)
        (cross / "OUTCAR").write_text("free energy TOTEN = -100 eV\nfree energy TOTEN = -96 eV\n",
                                     encoding="ascii")
        out = self.tmp / "cross-result"
        proc, payload = run_script("--fc-dir", CASE / "dispersion", "--gs-dir", CASE / "relax",
                                  "--es-dir", CASE / "excited" / "try_1_singlet",
                                  "--es-at-gs-dir", cross, "--zpl-formula", "E_es@gs + 100", "--out-dir", out)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["zpl_formula"], dict(text="E_es@gs + 100",
                                                   variables_eV={"E_es@gs": -96}, value_eV=4))
        self.assertEqual(summary["input_paths"]["es_at_gs_dir"], str(cross.resolve()))

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
            ["photon_energy_eV", "intensity_computed", "intensity_spin_purified", "intensity_experimental",
             "intensity_formula"],
        )
        data = np.array(rows, dtype=float)
        energy = data[:, 0]
        zpl = self.summary()["zpl_eV"]
        np.testing.assert_allclose(data[:, 4], data[:, 2])
        for column, key in ((1, "computed"), (2, "computed_spin_purified"), (3, "experimental"), (4, "formula")):
            intensity = data[:, column]
            self.assertAlmostEqual(np.nanmax(intensity), 1.0, places=6)
            # T = 0 emission: almost no weight above the ZPL beyond the Lorentzian tail
            above = np.trapezoid(np.where(energy > zpl[key] + 0.05, intensity, 0.0), energy)
            total = np.trapezoid(intensity, energy)
            self.assertLess(above / total, 0.02, key)


BULK = ROOT / "test-directory" / "sjcm_mgr" / "hBN" / "bulk" / "phonons" / "vasp_661"


@unittest.skipIf(IMPORT_ERROR is not None, f"dephonopy/numpy missing: {IMPORT_ERROR}")
@unittest.skipUnless((CASE / "dispersion").is_dir() and BULK.is_dir(),
                     "defect and bulk reference data in test-directory/ not present")
class EmbeddingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = cls.enterClassContext(temporary_directory())
        cls.out = cls.tmp / "embedded"
        cls.small_out = cls.tmp / "nonembedded"
        args = (
            "--fc-dir", CASE / "dispersion",
            "--gs-dir", CASE / "relax",
            "--es-dir", CASE / "excited" / "try_1_singlet",
            "--triplet-dir", CASE / "excited" / "try_1_tryplet",
            "--zpl-exp", 4.08,
        )
        cls.proc, cls.payload = run_script(
            *args, "--bulk-fc-dir", BULK, "--mult", "12x12x1", "--out-dir", cls.out,
        )
        cls.small_proc, cls.small_payload = run_script(*args, "--out-dir", cls.small_out)

    def setUp(self):
        self.assertEqual(self.proc.returncode, 0, self.proc.stderr[-3000:])
        self.assertTrue(self.payload["ok"])

    def summary(self):
        return json.loads((self.out / "summary.json").read_text(encoding="utf-8"))

    def test_sizes_and_embedding_metadata(self):
        summary = self.summary()
        self.assertEqual(summary["n_atoms"], 288)
        self.assertEqual(summary["n_modes"], 864)
        embedding = summary["embedding"]
        self.assertEqual(embedding["defect_sites"], 2)
        self.assertEqual(embedding["mult"], [12, 12, 1])
        self.assertEqual(summary["label"], "try_1_singlet (emb 12x12x1)")
        self.assertIsNone(summary["zpl_formula"])
        self.assertTrue(np.isnan(np.loadtxt(self.out / "pl.tsv", skiprows=1)[:, 4]).all())
        self.assertEqual(Path(embedding["bulk_fc_dir"]), BULK.resolve())
        self.assertGreater(embedding["dcutoff_A"], 0)
        self.assertGreater(embedding["bcutoff_A"], 0)

    def test_origin_alignment(self):
        from ase.geometry import find_mic

        embedding = self.summary()["embedding"]
        structure = json.loads((self.out / "structure.json").read_text(encoding="utf-8"))
        primitive = np.asarray(structure["cell_A"]) / np.asarray(embedding["mult"])[:, None]
        difference = np.asarray(embedding["origin_shift_A"]) - [1.25, 0.7217, 0]
        self.assertLess(find_mic(difference, primitive)[1], 0.05)

    def test_huang_rhys_and_dominant_mode(self):
        self.assertAlmostEqual(self.summary()["s_total"], 1.4217, delta=0.005)
        header, rows = read_tsv(self.out / "modes.tsv")
        self.assertEqual(len(rows), 864)
        col = {name: i for i, name in enumerate(header)}
        top = max(rows, key=lambda row: float(row[col["S_k"]]))
        self.assertAlmostEqual(float(top[col["freq_meV"]]), 199.81, delta=0.1)
        self.assertEqual(top[col["irrep"]], "A1")
        self.assertAlmostEqual(float(top[col["S_k"]]), 0.537, delta=0.005)

    def check_poscar(self, directory, n_atoms):
        from ase.io import read

        self.assertTrue((directory / "POSCAR").is_file())
        atoms = read(directory / "POSCAR", format="vasp")
        structure = json.loads((directory / "structure.json").read_text(encoding="utf-8"))
        self.assertEqual(len(atoms), n_atoms)
        self.assertEqual(atoms.get_chemical_symbols(), structure["symbols"])
        np.testing.assert_allclose(atoms.get_positions(), structure["positions_A"], atol=1e-10)
        np.testing.assert_allclose(atoms.get_cell(), structure["cell_A"], atol=1e-10)

    def test_embedded_poscar(self):
        self.check_poscar(self.out, 288)

    def test_nonembedded_summary_and_poscar(self):
        self.assertEqual(self.small_proc.returncode, 0, self.small_proc.stderr[-3000:])
        self.assertTrue(self.small_payload["ok"])
        summary = json.loads((self.small_out / "summary.json").read_text(encoding="utf-8"))
        self.assertIsNone(summary["embedding"])
        self.assertEqual(summary["n_atoms"], 162)
        self.assertEqual(summary["label"], "try_1_singlet")
        self.assertIsNone(summary["zpl_formula"])
        self.check_poscar(self.small_out, 162)

    def test_embedded_eigenvectors_and_participation(self):
        vectors = np.load(self.out / "eigenvectors.npy")
        participation = np.load(self.out / "participation.npy")
        self.assertEqual(vectors.shape, (864, 288, 3))
        self.assertEqual(participation.shape, (864, 288))
        np.testing.assert_allclose(participation.sum(axis=1), 1, atol=1e-4)
        np.testing.assert_allclose((vectors.astype(np.float64) ** 2).sum(axis=2), participation, atol=1e-4)

    def test_invalid_embedding_options_leave_no_output(self):
        with temporary_directory() as tmp:
            out = Path(tmp) / "result"
            for options in (("--bulk-fc-dir", BULK), ("--mult", "12x12x1"),
                            ("--bulk-fc-dir", BULK, "--mult", "12x0x1"),
                            ("--bulk-fc-dir", BULK, "--mult", "12x12x1", "--dcutoff", "nan"),
                            ("--bulk-fc-dir", BULK, "--mult", "12x12x1", "--bcutoff", "-1")):
                with self.subTest(options=options):
                    proc, payload = run_script(
                        "--fc-dir", CASE / "dispersion", "--gs-dir", CASE / "relax",
                        "--es-dir", CASE / "excited" / "try_1_singlet", "--out-dir", out, *options,
                    )
                    self.assertNotEqual(proc.returncode, 0)
                    self.assertIsNotNone(payload, proc.stderr[-2000:])
                    self.assertFalse(payload["ok"])
                    self.assertTrue(payload["error"])
                    self.assertFalse(out.exists())

    def test_hr_methods_cutoffs_and_reference_sizes(self):
        from phonon_vibronic_analysis import analyse

        plain = json.loads((self.small_out / "summary.json").read_text(encoding="utf-8"))
        embedded = self.summary()
        self.assertEqual(plain["hr_method"], "displacements")
        self.assertIsNone(plain["hr_cutoff_A"])
        self.assertIsNone(plain["sigma_max_meV"])
        self.assertGreater(embedded["hr_cutoff_A"], 0)
        print(f"113b displacements none: S={plain['s_total']:.10f}")
        print(f"113b displacements embedded12 auto: S={embedded['s_total']:.10f}, cutoff={embedded['hr_cutoff_A']:.10f}")
        auto = analyse(CASE / "dispersion", CASE / "relax", CASE / "excited/try_1_singlet",
                       bulk_fc_dir=BULK, mult=(9, 9, 1))["summary"]
        self.assertAlmostEqual(auto["s_total"], 1.422, delta=.01)
        print(f"113b displacements embedded9 auto: S={auto['s_total']:.10f}, cutoff={auto['hr_cutoff_A']:.10f}")
        result = analyse(CASE / "dispersion", CASE / "relax", CASE / "excited/try_1_singlet",
                         hr_cutoff=embedded["hr_cutoff_A"], sigma_max=8.)
        self.assertAlmostEqual(result["summary"]["hr_cutoff_A"], embedded["hr_cutoff_A"])
        self.assertEqual(result["summary"]["sigma_max_meV"], 8.)
        print(f"113b displacements explicit auto radius, FC cell: S={result['summary']['s_total']:.10f}")
        for mult, expected in ((None, .7883808316), ((12, 12, 1), .8237285540), ((24, 24, 1), .8760143534)):
            result = analyse(CASE / "dispersion", CASE / "relax", CASE / "excited/try_1_singlet",
                             bulk_fc_dir=BULK, mult=mult, hr_method="approximate_forces")
            summary = result["summary"]
            self.assertEqual(summary["hr_method"], "approximate_forces")
            self.assertTrue(math.isfinite(summary["s_total"]))
            self.assertGreater(summary["s_total"], 0)
            self.assertAlmostEqual(summary["s_total"], expected, delta=.005)
            self.assertAlmostEqual(float(result["s_k"].sum()), summary["s_total"])
            print(f"113b approximate_forces {mult or 'FC cell'}: S={summary['s_total']:.10f}, cutoff={summary['hr_cutoff_A']}")

    def test_force_method_uses_emission_order_and_library_factors(self):
        from dephonopy.hr_factors.io import read_atomic_displacements
        from dephonopy.hr_factors.fc_displacements import hr_factors_from_forces
        from phonon_vibronic_analysis import analyse

        _, _, es = read_atomic_displacements(str(CASE / "relax/CONTCAR"),
                                             str(CASE / "excited/try_1_singlet/CONTCAR"), verbose=False)
        forces = np.zeros((len(es), 3))
        forces[:2, 0] = [1e-4, -1e-4]
        reference = {}
        def calculate(*args, **kwargs):
            reference.update(hr_factors_from_forces(*args, **kwargs))
            return reference
        with patch("dephonopy.hr_factors.io.read_force_differences", return_value=(forces, es, es)) as reader, \
                patch("dephonopy.hr_factors.fc_displacements.hr_factors_from_forces", side_effect=calculate) as hr:
            result = analyse(CASE / "dispersion", CASE / "relax", CASE / "excited/try_1_singlet",
                             gs_at_es_dir=CASE / "relax", hr_method="forces")
        reader.assert_called_once_with(str((CASE / "excited/try_1_singlet").resolve()),
                                       str((CASE / "relax").resolve()), check_equal_positions=True)
        self.assertEqual(result["summary"]["hr_method"], "forces")
        self.assertEqual(hr.call_count, 1)
        self.assertGreater(result["summary"]["s_total"], 0)
        np.testing.assert_array_equal(result["s_k"][3:], reference["hr_factors"])


if __name__ == "__main__":
    unittest.main()
