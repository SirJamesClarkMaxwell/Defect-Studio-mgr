"""Task 106a: real hBN bulk data is optional; failure and path tests are independent."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from uuid import uuid4

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts/python/examples/phonon_dispersion.py"
BULK = ROOT / "test-directory/sjcm_mgr/hBN/bulk/phonons/vasp_661"


@pytest.fixture
def work_dir():
    # mkdir inherits ACLs; Python 3.13 TemporaryDirectory blocks sandbox access on Windows.
    directory = Path(tempfile.gettempdir()) / f"ds-phonon-dispersion-{uuid4().hex}"
    directory.mkdir()
    try:
        yield directory
    finally:
        shutil.rmtree(directory)


def run_script(*args):
    proc = subprocess.run([sys.executable, str(SCRIPT), *map(str, args)],
                          capture_output=True, text=True, encoding="utf-8", timeout=600)
    lines = [line for line in proc.stdout.splitlines() if line.strip()]
    assert len(lines) == 1, proc.stdout
    return proc, json.loads(lines[-1])


@pytest.fixture(params=[(101, False), (11, True)], ids=["symmetrized", "no-symmetrize"])
def bulk_result(work_dir, request):
    if not (BULK / "dephonopy_disp.yaml").is_file():
        pytest.skip("hBN bulk reference data in test-directory/ not present")
    pytest.importorskip("dephonopy")
    out = work_dir / "result"
    npoints, no_symmetrize = request.param
    args = ["--fc-dir", BULK, "--npoints", npoints, "--out-dir", out]
    if no_symmetrize:
        args.append("--no-symmetrize")
    proc, payload = run_script(*args)
    assert proc.returncode == 0, proc.stderr[-3000:]
    assert payload["ok"], payload
    return out, payload, not no_symmetrize


def test_hbn_bulk_dispersion(bulk_result):
    out, payload, symmetrized = bulk_result
    summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
    assert summary["schema"] == "ds.phonon_dispersion.v1"
    assert summary["n_bands"] == 3 * summary["n_atoms_primitive"] == 6
    assert summary["primitive_symbols"] == ["B", "N"]
    assert np.asarray(summary["primitive_cell_A"]).shape == (3, 3)
    assert summary["symmetrized"] is symmetrized
    assert summary["label"] == BULK.name
    assert (out / summary["inputs_relative"]["fc_dir"]).resolve() == BULK.resolve()
    assert summary["ticks"][0]["label"] == "Γ"
    assert np.count_nonzero(np.abs(summary["gamma_freq_meV"]) < 1) == 3
    assert 150 < summary["max_freq_meV"] < 220
    assert Path(payload["out_dir"]) == out.resolve()
    for key in ("n_bands", "min_freq_meV", "max_freq_meV"):
        assert payload[key] == summary[key]
    with (out / "dispersion.tsv").open(encoding="utf-8") as handle:
        assert handle.readline().strip().split("\t") == [
            "segment", "distance", *[f"band_{i}" for i in range(summary["n_bands"])]]
    table = np.loadtxt(out / "dispersion.tsv", skiprows=1)
    assert table.shape[1] == summary["n_bands"] + 2
    assert np.isfinite(table).all()
    assert np.all(np.diff(table[:, 1]) >= 0)
    transitions = np.flatnonzero(np.diff(table[:, 0])) + 1
    np.testing.assert_allclose(table[transitions, 1], summary["breaks"])
    assert len(transitions) == 2


def test_missing_directory_reports_json_and_publishes_nothing(work_dir):
    out = work_dir / "result"
    proc, payload = run_script("--fc-dir", work_dir / "missing", "--out-dir", out)
    assert proc.returncode == 1
    assert payload["ok"] is False
    assert payload["error"]
    assert not out.exists()
    assert list(work_dir.iterdir()) == []


@pytest.mark.parametrize("args", [[], ["--npoints", "bad"], ["--npoints", "1"]])
def test_argument_errors_are_json_and_preserve_previous_output(work_dir, args):
    out = work_dir / "result"
    out.mkdir()
    previous = out / "previous.txt"
    previous.write_text("keep", encoding="utf-8")
    base = [] if not args else ["--fc-dir", work_dir, "--out-dir", out]
    proc, payload = run_script(*base, *args)
    assert proc.returncode == 1
    assert payload["ok"] is False
    assert payload["error"]
    assert previous.read_text(encoding="utf-8") == "keep"


def test_path_breaks_gamma_at_endpoint_and_signed_energies(monkeypatch):
    pytest.importorskip("dephonopy")
    monkeypatch.syspath_prepend(str(SCRIPT.parent))
    from phonon_dispersion import band_data

    # The first Gamma is an endpoint; the second piece starts elsewhere at the same distance.
    phonon = SimpleNamespace(
        primitive=SimpleNamespace(symbols=["B"], cell=np.eye(3)),
        band_structure=SimpleNamespace(path_connections=[True, False, False],
                                       labels=["M", r"$\Gamma$", "K", "A", "GAMMA"]),
        get_band_structure_dict=lambda: {
            "distances": [np.array([0., 1.]), np.array([1., 2.]), np.array([2., 3.])],
            "frequencies": [np.array([[-.001, .1, .2], [0., .002, .003]])] * 3,
            "qpoints": [np.array([[.5, 0, 0], [0., 0, 0]])] * 3})
    result = band_data(phonon)
    summary = result["summary"]
    assert [tick["label"] for tick in summary["ticks"]] == ["M", "Γ", "K/A", "Γ"]
    assert summary["breaks"] == [2.]
    assert summary["gamma_freq_meV"] == pytest.approx([0., 2., 3.])
    assert summary["min_freq_meV"] == pytest.approx(-1.)
    np.testing.assert_array_equal(result["dispersion"][:, 0], [0, 0, 0, 0, 1, 1])


def test_in_plane_path_keeps_only_qz_zero_segments(work_dir):
    if not (BULK / "dephonopy_disp.yaml").is_file():
        pytest.skip("hBN bulk reference data in test-directory/ not present")
    out = work_dir / "result"
    proc, payload = run_script("--fc-dir", BULK, "--npoints", 21, "--in-plane",
                               "--label", "hBN α", "--out-dir", out)
    assert proc.returncode == 0, proc.stderr[-3000:]
    summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
    assert [tick["label"] for tick in summary["ticks"]] == ["Γ", "M", "K", "Γ"]
    assert summary["breaks"] == []
    assert summary["in_plane"] is True
    assert summary["label"] == "hBN α"
    assert 150 < summary["max_freq_meV"] < 220
