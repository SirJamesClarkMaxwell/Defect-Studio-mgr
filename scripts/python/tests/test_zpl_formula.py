"""Restricted arithmetic over named final TOTEN energies."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "examples"))
from zpl_formula import PRESETS, VARIABLES, evaluate, used_variables


def test_presets_and_cross_geometry_names():
    energies = dict(E_gs=-100, E_es=-98, E_T=-97.7, **{"E_es@gs": -97, "E_gs@es": -99})
    assert evaluate(PRESETS["dscf"], energies) == 2
    assert evaluate(PRESETS["spin_purified"], energies) == pytest.approx(1.7)
    assert evaluate("E_es@gs - E_gs", energies) == 3
    assert evaluate("+(E_gs@es - E_gs) / 2", energies) == .5
    assert used_variables("E_gs@es + E_es@gs - E_es + E_gs - E_gs") == [
        "E_gs", "E_es", "E_es@gs", "E_gs@es"]
    assert VARIABLES == ("E_gs", "E_es", "E_T", "E_es@gs", "E_gs@es")


@pytest.mark.parametrize("formula", ["__import__('os')", "E_es**2", "abs(E_es)", "True",
                                     "'1.945'", "E_es.real", "[E_es]", "E_es // 2",
                                     "E_es % 2", "E_es @ E_gs", "1 if E_es else 2", "("])
def test_rejects_non_arithmetic(formula):
    with pytest.raises(ValueError, match="Niedozwolony element we wzorze ZPL"):
        evaluate(formula, {"E_es": 2})
    with pytest.raises(ValueError):
        used_variables(formula)


def test_unknown_name_lists_allowed_names():
    with pytest.raises(ValueError) as error:
        evaluate("E_x", {})
    assert all(name in str(error.value) for name in VARIABLES)


def test_missing_triplet():
    with pytest.raises(ValueError, match="Wzór używa E_T, ale nie podano katalogu trypletu"):
        evaluate(PRESETS["spin_purified"], {"E_gs": -100, "E_es": -98, "E_T": None})


def test_experimental_constant():
    assert evaluate("1.945", {}) == 1.945
    assert used_variables("1.945") == []


@pytest.mark.parametrize("formula", ["-1", "0", "1e309", "1 / 0", "1e308 * 2"])
def test_invalid_result(formula):
    with pytest.raises(ValueError):
        evaluate(formula, {})


@pytest.mark.parametrize("value", [float("nan"), float("inf")])
def test_nonfinite_energy(value):
    with pytest.raises(ValueError):
        evaluate("E_es", {"E_es": value})
