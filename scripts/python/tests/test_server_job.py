"""Small independent templates for the cluster-folder generator (no user asset fixtures)."""
from contextlib import contextmanager
import json
from pathlib import Path
import py_compile
import shutil
import subprocess
import sys
from types import SimpleNamespace
from unittest.mock import patch
from uuid import uuid4

import pytest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/python/examples"))
import server_job as job


@pytest.fixture
def directory():
    path = ROOT / ".task-113d-temp" / uuid4().hex
    path.mkdir(parents=True)
    try:
        yield path
    finally:
        shutil.rmtree(path)


class Geometry:
    def write(self, path, format):
        assert format == "vasp"
        Path(path).write_text("aligned test geometry", encoding="utf-8")


@pytest.fixture
def options(directory):
    templates = directory / "assets"
    templates.mkdir()
    # These are deliberately tiny, original fake templates, unrelated to user script text.
    config = ("from pathlib import Path\nHERE = Path(__file__).parent\n# ===== CONFIG =====\n"
              "SYSTEM = None\nBULK_PHONONS = None\nINIT = None\nFINAL = None\nMETHOD = 'old'\n"
              "SIZES = []\nCUTOFFS = []\nSIG = 0\nSIG_MAX = 0\nGAMMA = 0\nZPL = 0\n"
              "HR_DIR = ''\nUNRELATED = ('keep', 42)  # unchanged\n# ==================\n"
              "sentinel = 'outside config'\n")
    for name in ("embeding.py", "hr_factors.py", "lineshape.py"):
        (templates / name).write_text(config, encoding="utf-8")
    (templates / "run_embeding_series.py").write_text(
        "N_MIN, N_MAX = 1, 2  # bound comment\nOTHER = 73\n", encoding="utf-8")
    for name in ("run.job", "run_embeding.job"):
        (templates / name).write_bytes(b"#!/bin/sh\r\n# fake scheduler\r\n")
    (templates / "extra").mkdir()
    (templates / "extra/data.bin").write_bytes(bytes(range(20)))
    inputs = {}
    for role in ("fc", "bulk_fc", "gs", "es"):
        inputs[role + "_dir"] = directory / role
        inputs[role + "_dir"].mkdir()
    return SimpleNamespace(**inputs, templates_dir=templates, out_dir=directory / "job", n_min=11, n_max=13,
                           sigma=2.0, sigma_max=8.0, gamma=0.5, hr_method="displacements", hr_cutoff=4.0,
                           dcutoff=None, bcutoff=None, zpl=3450.0, zpl_formula=None, label="test",
                           triplet_dir=None, gs_at_es_dir=None, es_at_gs_dir=None)


@contextmanager
def fake_geometry():
    with patch.object(job, "prepare_geometry", return_value=((Geometry(), Geometry()), [0.1, 0.2, 0.0], 3450.0, 9)):
        yield


def test_generated_scripts_compile_values_portable_and_assets_untouched(options):
    original = {p: p.read_bytes() for p in options.templates_dir.rglob("*") if p.is_file()}
    with fake_geometry():
        output = job.generate(options)
    for script in output.rglob("*.py"):
        py_compile.compile(str(script), doraise=True)
        assert not __import__("re").search(r"[A-Za-z]:[\\/]", script.read_text())
    namespace = {}
    exec((output / "lineshape.py").read_text(), {"__file__": str(output / "lineshape.py")}, namespace)
    assert namespace["SIZES"] == [11, 12, 13]
    assert namespace["CUTOFFS"] == [4.0]
    assert (namespace["SIG"], namespace["SIG_MAX"], namespace["GAMMA"], namespace["ZPL"]) == (2, 8, .5, 3450)
    assert namespace["HR_DIR"] == "displacements_cutoff4A"
    assert namespace["SYSTEM"] == output
    assert namespace["INIT"] == output / "inputs/es/CONTCAR_aligned"
    assert namespace["BULK_PHONONS"].resolve() == options.bulk_fc_dir
    series = {}
    exec((output / "run_embeding_series.py").read_text(), series)
    assert (series["N_MIN"], series["N_MAX"], series["OTHER"]) == (11, 13, 73)
    assert (output / "inputs/gs/CONTCAR_aligned").is_file()
    assert (output / "extra/data.bin").read_bytes() == bytes(range(20))
    assert (output / "run.job").read_bytes() == original[options.templates_dir / "run.job"]
    assert all(p.read_bytes() == data for p, data in original.items())
    manifest = json.loads((output / "job.json").read_text())
    assert manifest["origin_shift_A"] == [0.1, 0.2, 0]
    assert "SIG" in manifest["configured"]["lineshape.py"]
    assert "UNRELATED" not in manifest["configured"]["lineshape.py"]
    readme = (output / "README.txt").read_text()
    for command in ("sbatch run_embeding.job 11x11x1", "nohup python3 run_embeding_series.py", "sbatch run.job hr_factors.py", "python lineshape.py"):
        assert command in readme


def test_only_known_assignments_change_and_non_config_is_identical():
    text = ("# outside\r\n# ===== CONFIG =====\r\nSIG: float = 1  # note\r\n"
            "UNKNOWN = [\r\n    3, 4\r\n]\r\n# ==================\r\nSIG = 99\r\n")
    rendered, names = job.configure(text, {"SIG": "2", "ABSENT": "7"})
    assert rendered == text.replace("float = 1", "float = 2")
    assert names == ["SIG"]
    assert job.configure("x = 1\n", {"x": "2"}) == ("x = 1\n", [])
    with pytest.raises(ValueError, match="closing marker"):
        job.configure("# ===== CONFIG =====\nSIG = 1\n", {"SIG": "2"})


@pytest.mark.parametrize("key,value,error", [("n_min", 0, "n-min"), ("n_max", 1, "n-max"),
    ("sigma", float("nan"), "sigma"), ("sigma_max", 1, "sigma-max"),
    ("gamma", 0, "gamma"), ("hr_cutoff", -1, "hr_cutoff"), ("hr_method", "forces", "gs-at-es-dir")])
def test_validation_does_not_write(options, key, value, error):
    setattr(options, key, value)
    with pytest.raises(ValueError, match=error):
        job.generate(options)
    assert not options.out_dir.exists()


def test_missing_templates_and_no_overwrite(options):
    options.templates_dir = options.templates_dir / "missing"
    with pytest.raises(ValueError, match=str(options.templates_dir).replace("\\", "\\\\")):
        job.generate(options)
    options.templates_dir = options.templates_dir.parent
    options.out_dir.mkdir()
    marker = options.out_dir / "keep.txt"
    marker.write_text("precious")
    with pytest.raises(ValueError, match="not be replaced"):
        job.generate(options)
    assert marker.read_text() == "precious"
    options.out_dir = options.fc_dir.parent
    with pytest.raises(ValueError, match="contain an input"):
        job.generate(options)


def test_failed_write_cleans_only_chosen_folder(options):
    def fail(*args, **kwargs):
        raise OSError("test write failure")
    with fake_geometry(), patch.object(Geometry, "write", fail), pytest.raises(OSError, match="test write failure"):
        job.generate(options)
    assert list(options.out_dir.iterdir()) == []
    assert (options.templates_dir / "run.job").exists()


def test_cross_drive_mapping_is_portable(directory):
    with patch.object(job.os.path, "relpath", side_effect=ValueError("different drive")):
        assert job.relative_input(directory, directory, "bulk") == "../cluster-inputs/bulk"


def test_series_and_scientific_compatibility_are_explicit(options):
    # Original executable miniature, not text taken from user assets.
    text = ("from pathlib import Path\nHERE = Path('.')\nSYSTEM = HERE\n"
            "fc = ForceConstants(SYSTEM / 'fc-placeholder')\n"
            "big, info = construct_and_embed_defect_mpi(bulk_fc, (11,11,1), fc, unit_cell=old_cell)\n")
    options.dcutoff = 3.5
    paths = {"FC_DIR": "HERE / '../fc'", "BULK_PHONONS": "HERE / '../bulk'"}
    rendered, changes = job.adapt(text, "embeding.py", paths, [1, 0, 0], options, 7)
    compile(rendered, "test", "exec")
    assert "fc.atoms.set_positions" in rendered
    assert "unit_cell=bulk_fc.primitive_cell" in rendered
    assert "dcutoff=3.5" in rendered
    assert "defect FC path decoupled from output SYSTEM" in changes
    series, changes = job.adapt("OUT_ROOT = None\nN_MIN = 1\n", "run_embeding_series.py", paths, [], options, 9)
    assert 'OUT_ROOT = HERE / "embedded"' in series
    assert changes
    for method in ("forces", "approximate_forces"):
        options.hr_method = method
        rendered, changes = job.adapt("data = calc_hr_factors(modes, a, b)\n", "hr_factors.py", paths, [1, 0, 0], options, 9)
        compile(rendered, "test", "exec")
        assert ("shift_defect" if method == "forces" else "unit_cell_phonons") in rendered
        assert changes
    rendered, changes = job.adapt("def reference():\n    modes = fc.calculate_modes()\n    return modes\n",
                                  "hr_factors.py", paths, [1, 0, 0], options, 9)
    compile(rendered, "test", "exec")
    assert "reference FC geometry aligned like GS/ES" in changes


def test_alignment_reuses_local_origin_shift(options):
    np = pytest.importorskip("numpy")
    pytest.importorskip("dephonopy")
    from ase import Atoms
    cell = np.diag([4., 4., 8.])
    bulk = Atoms("BN", positions=[[0, 0, 0], [2, 2, 0]], cell=cell, pbc=True)
    defect = bulk.copy()
    defect.positions += [.3, .4, 0]
    gs, es = defect.copy(), defect.copy()
    es.positions[1, 0] += .05
    fc = SimpleNamespace(atoms=defect)
    bulk_fc = SimpleNamespace(atoms=bulk, primitive_cell=bulk)
    with patch("dephonopy.force_constants.ForceConstants", side_effect=[fc, bulk_fc]), patch(
            "dephonopy.hr_factors.io.read_atomic_displacements", return_value=(None, gs, es)):
        geometries, shift, zpl, n = job.prepare_geometry(options)
    np.testing.assert_allclose(shift, [.3, .4, 0], atol=1e-12)
    np.testing.assert_allclose(geometries[0].positions, bulk.positions, atol=1e-12)
    assert geometries[1].positions[1, 0] == pytest.approx(2.05)
    assert zpl == 3450


def test_cli_reports_json_error_without_scientific_imports(directory):
    result = subprocess.run([sys.executable, str(Path(job.__file__)), "--unknown"], capture_output=True, text=True)
    assert result.returncode == 1
    assert json.loads(result.stdout)["ok"] is False


def test_folder_race_does_not_overwrite_or_remove_foreign_file(options):
    def geometry_after_race(*args):
        options.out_dir.mkdir()
        (options.out_dir / "foreign.txt").write_text("keep")
        return (Geometry(), Geometry()), [0, 0, 0], 3450, 9
    with patch.object(job, "prepare_geometry", side_effect=geometry_after_race), pytest.raises(ValueError, match="no longer empty"):
        job.generate(options)
    assert (options.out_dir / "foreign.txt").read_text() == "keep"


def test_absolute_windows_path_is_not_silently_exported(options):
    (options.templates_dir / "bad.py").write_text("VALUE = 'Z:/private/calculation'\n")
    with fake_geometry(), pytest.raises(ValueError, match="Windows path remains"):
        job.generate(options)
    assert not options.out_dir.exists()


def test_permission_fallback_only_when_requested(directory):
    fallback = directory / "project-job"
    with patch.object(sys, "argv", ["server_job.py", "--templates-dir", "assets", "--fc-dir", "fc",
            "--bulk-fc-dir", "bulk", "--gs-dir", "gs", "--es-dir", "es", "--out-dir", "blocked",
            "--n-min", "10", "--n-max", "11", "--fallback-dir", str(fallback)]), patch.object(
            job, "generate", side_effect=[PermissionError("read only"), fallback]) as generate:
        assert job.main() == 0
        assert generate.call_count == 2
        assert generate.call_args.args[0].out_dir == fallback


def test_optional_unavailable_values_remain_template_defaults(options):
    asset = options.templates_dir / "embeding.py"
    asset.write_text(asset.read_text().replace("SYSTEM = None", "SYSTEM = None\nDCUTOFF = 7.0"))
    with fake_geometry():
        output = job.generate(options)
    assert "DCUTOFF = 7.0" in (output / "embeding.py").read_text()
    assert "DCUTOFF" not in json.loads((output / "job.json").read_text())["configured"]["embeding.py"]
