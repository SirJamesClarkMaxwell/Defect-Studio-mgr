from __future__ import annotations

import json
import pathlib
import math
import re
import sys


def _band_gap_payload(output) -> dict | None:
    import numpy as np
    # The new aggregate reader exposes eigenvalues/occupations publicly and falls back from
    # vasprun.xml to EIGENVAL. Preserve puntukas' global all-spin/all-kpoint band-edge convention.
    eigenvalues = output.get_eigenvalues()
    occupations = output.get_occupations()
    if eigenvalues is None or occupations is None:
        return None
    occupied = ~np.isclose(occupations, 0)
    unoccupied = np.isclose(occupations, 0)
    if not occupied.any() or not unoccupied.any():
        return None
    homo = float(eigenvalues[occupied].max())
    lumo = float(eigenvalues[unoccupied].min())
    return {"bandgap": lumo - homo, "homo": homo, "lumo": lumo}


_NUMBER = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[EeDd][-+]?\d+)?"


def _number(value: str) -> float:
    return float(value.replace("D", "E").replace("d", "e"))


def _scan_outcar(path: pathlib.Path, window: tuple[int, int] | None) -> dict:
    summary = {"energy_trend": [], "force_trend": [], "energy_without_entropy": [],
               "energy_sigma_zero": [], "ks_steps": [], "finished": False,
               "reached_accuracy": False, "outcar_write_time": path.stat().st_mtime}
    parameters = {"ISPIN": "ispin", "NSW": "nsw", "IBRION": "ibrion", "EDIFFG": "ediffg",
                  "NBANDS": "nbands", "NKPTS": "nkpts", "NIONS": "nions", "NELECT": "nelect"}
    integer_parameters = {"ISPIN", "NSW", "IBRION", "NBANDS", "NKPTS", "NIONS"}
    ionic = None
    frame = {}
    blocks = []
    block = None
    spin, kpoint = 1, 1
    force_count, force_max, force_active = 0, 0.0, False

    def finish_block(terminated: bool):
        nonlocal block
        if block is not None:
            expected = summary.get("nbands")
            block["complete"] = (block["count"] == expected if expected is not None
                                 else terminated and block["count"] > 0)
            blocks.append(block)
            block = None

    def finish_step():
        nonlocal frame, blocks, spin, kpoint, force_active
        finish_block(False)
        if "energy" in frame:
            summary["energy_trend"].append(frame["energy"])
            for key in ("force_trend", "energy_without_entropy", "energy_sigma_zero"):
                summary[key].append(frame.get(key))
            expected = {(s, k) for s in range(1, summary.get("ispin", 1) + 1)
                        for k in range(1, summary.get("nkpts", 1) + 1)}
            if blocks and all(b["complete"] for b in blocks) and \
                    {(b["spin"], b["kpoint"]) for b in blocks} == expected and len(blocks) == len(expected):
                occupied = [b["homo"] for b in blocks if b["homo"] is not None]
                empty = [b["lumo"] for b in blocks if b["lumo"] is not None]
                if occupied and empty:
                    summary["homo_band"] = max(v[0] for v in occupied)
                    summary["lumo_band"] = min(v[0] for v in empty)
                    summary["ks_gap"] = {"homo": max(v[1] for v in occupied),
                                         "lumo": min(v[1] for v in empty)}
                if window is not None:
                    summary["ks_steps"].append({"step": len(summary["energy_trend"]),
                        "fermi": frame.get("fermi"), "blocks": [
                            {key: b[key] for key in ("spin", "kpoint", "bands", "energies", "occupations")}
                            for b in blocks]})
        frame, blocks = {}, []
        spin, kpoint, force_active = 1, 1, False

    with path.open(encoding="utf-8", errors="replace") as source:
        for line in source:
            iteration = re.search(r"Iteration\s+(\d+)\s*\(", line)
            if iteration:
                next_ionic = int(iteration[1])
                if ionic is not None and ionic != next_ionic:
                    finish_step()
                ionic = next_ionic
            if "General timing and accounting" in line:
                summary["finished"] = True
            if "reached required accuracy" in line:
                summary["reached_accuracy"] = True
            for param, key in parameters.items():
                if param in line:
                    match = re.search(rf"\b{param}\s*=\s*({_NUMBER})", line)
                    if match:
                        summary[key] = int(_number(match[1])) if param in integer_parameters else _number(match[1])
            # Exact whitespace distinguishes ionic totals from electronic SCF totals.
            if "free  energy   TOTEN" in line:
                match = re.search(rf"free  energy   TOTEN\s*=\s*({_NUMBER})", line)
                if match:
                    if "energy" in frame:
                        finish_step()
                    frame["energy"] = _number(match[1])
            if "energy  without entropy" in line:
                match = re.search(rf"energy  without entropy\s*=\s*({_NUMBER}).*energy\(sigma->0\)\s*=\s*({_NUMBER})", line)
                if match:
                    frame["energy_without_entropy"], frame["energy_sigma_zero"] = map(_number, match.groups())
            if "E-fermi" in line:
                match = re.search(rf"E-fermi\s*:\s*({_NUMBER})", line)
                if match:
                    if ionic is None and "energy" in frame and blocks:
                        finish_step()
                    frame["fermi"] = _number(match[1])
            if "TOTAL-FORCE" in line:
                force_count, force_max, force_active = 0, 0.0, True
            elif force_active:
                parts = line.split()
                if len(parts) == 6 and all(re.fullmatch(_NUMBER, p) for p in parts):
                    force_count += 1
                    force_max = max(force_max, math.sqrt(sum(_number(p) ** 2 for p in parts[3:])))
                elif force_count:
                    if force_count == summary.get("nions", force_count):
                        frame["force_trend"] = force_max
                    force_active = False
            if "spin component" in line:
                finish_block(True)
                match = re.search(r"spin component\s+(\d+)", line)
                if match:
                    spin = int(match[1])
            if "k-point" in line:
                match = re.search(r"k-point\s+(\d+)\s*:", line)
                if match:
                    finish_block(True)
                    kpoint = int(match[1])
            if "band No." in line and "occupation" in line:
                finish_block(True)
                block = {"spin": spin, "kpoint": kpoint, "count": 0, "bands": [],
                         "energies": [], "occupations": [], "homo": None, "lumo": None}
            elif block is not None:
                parts = line.split()
                if len(parts) == 3 and parts[0].isdigit() and all(re.fullmatch(_NUMBER, p) for p in parts[1:]):
                    band, energy, occupation = int(parts[0]), _number(parts[1]), _number(parts[2])
                    if band != block["count"] + 1:
                        finish_block(False)
                        continue
                    block["count"] += 1
                    if occupation > 1e-5:
                        if block["homo"] is None or energy > block["homo"][1]:
                            block["homo"] = (band, energy)
                    elif block["lumo"] is None or energy < block["lumo"][1]:
                        block["lumo"] = (band, energy)
                    if window is not None and window[0] <= band <= window[1]:
                        block["bands"].append(band)
                        block["energies"].append(energy)
                        block["occupations"].append(occupation)
                else:
                    finish_block(True)
            for text, key in (("Total CPU time used", "cpu_time"), ("User time", "user_time"),
                              ("System time", "system_time"), ("Elapsed time", "elapsed_time")):
                if text in line:
                    match = re.search(rf":\s*({_NUMBER})", line)
                    if match:
                        summary[key] = _number(match[1])
            if "total drift:" in line:
                values = re.findall(_NUMBER, line.split("total drift:", 1)[1])
                if len(values) == 3:
                    summary["total_drift"] = list(map(_number, values))
            if "external pressure" in line:
                match = re.search(rf"external pressure\s*=\s*({_NUMBER})", line)
                if match:
                    summary["pressure"] = _number(match[1])
            if "in kB" in line:
                values = re.findall(_NUMBER, line.split("in kB", 1)[1])
                if len(values) == 6:
                    xx, yy, zz, xy, yz, zx = map(_number, values)
                    summary["stress_tensor"] = [[xx, xy, zx], [xy, yy, yz], [zx, yz, zz]]
    finish_step()
    if summary["energy_trend"]:
        summary["final_energy"] = summary["energy_trend"][-1]
    return summary


def _summary_payload(output, directory: pathlib.Path, band_start=0, band_end=0,
                     include_ks=False, outcar_name="OUTCAR") -> dict:
    summary = dict.fromkeys(("energy_trend", "final_energy", "cpu_time", "user_time", "system_time",
        "elapsed_time", "total_drift", "nelect", "ispin", "pressure", "stress_tensor",
        "space_group_symbol", "space_group_number", "point_group_symbol", "point_group_schoenflies"))
    path = directory / outcar_name
    if path.exists():
        parsed = _scan_outcar(path, None)
        if include_ks:
            homo, lumo = parsed.get("homo_band", 1), parsed.get("lumo_band", 1)
            default_start = max(1, min(homo, lumo) - 8)
            default_end = min(max(homo, lumo) + 8, parsed.get("nbands", max(homo, lumo) + 8))
            if band_start <= 0 or band_end <= 0:
                band_start, band_end = default_start, default_end
            parsed = _scan_outcar(path, (band_start, band_end))
            parsed["ks_band_start"], parsed["ks_band_end"] = band_start, band_end
            parsed["ks_default_band_start"], parsed["ks_default_band_end"] = default_start, default_end
        summary.update(parsed)
    elif outcar_name == "OUTCAR":
        try:
            from puntukas.vasp import VasprunData
            vasprun = VasprunData.from_file(directory / "vasprun.xml")
            summary["energy_trend"] = [float(step[-1]) for step in vasprun.energies if len(step)]
            if summary["energy_trend"]:
                summary["final_energy"] = summary["energy_trend"][-1]
            for name in ("ISPIN", "NSW", "IBRION", "EDIFFG"):
                value = vasprun.parameters.get(name)
                summary[name.lower()] = (float(value) if name == "EDIFFG" else int(value)) if value is not None else None
        except Exception:
            pass
    else:
        raise FileNotFoundError(path)
    if output is not None:
        try:
            atoms = output.atoms
            sym = atoms.get_symmetry() if atoms is not None else None
            if sym is not None:
                summary.update(space_group_symbol=str(sym.international_symbol),
                    space_group_number=int(sym.spacegroup_number), point_group_symbol=str(sym.pointgroup),
                    point_group_schoenflies=str(sym.schoenflies))
        except (ImportError, AttributeError, TypeError, ValueError, RuntimeError):
            pass
    return summary


def _orbitals_payload(
    output, band_start: int, band_end: int, irreps: bool = False, irrep_tol: float = 1e-1,
    symprec: float = 1e-3) -> tuple[list[dict] | None, str | None]:
    # orbital_info raises FileNotFoundError if WAVECAR/vaspwave.h5 is absent or unreadable. The
    # aggregate reader records parse errors through check(), so those cases remain distinguishable.
    # ireps defaults to False (puntukas' own default): symmetry-labeling each band is real
    # per-band cost (symmetry analysis over the structure) - a wide band range with ireps=True was
    # observed to be dramatically slower than the same range without it. Caller (ElectronicStructurePanel's
    # "Show symmetry labels" toggle) opts in explicitly.
    # band_start/band_end are VASP's own 1-based, inclusive band numbers (matching OUTCAR/EIGENVAL
    # - what a user actually cross-checks against), but orbital_info indexes the WAVECAR's band
    # array 0-based and its "nr" column is that same raw index. Only band_start
    # needs the -1 shift going in - range(start, end)'s exclusive end already happens to line up
    # with an inclusive 1-based band_end. "nr" gets +1 coming back out so the reported band numbers
    # match VASP's, not the WAVECAR array position.
    try:
        rows = output.orbital_info(
            max(band_start - 1, 0), band_end, ireps=irreps, irep_tol=irrep_tol, symprec=symprec)
    except FileNotFoundError:
        for status in output.check():
            if status.name == "wavecar" and status.error is not None:
                return None, f"WAVECAR/vaspwave.h5 present but unreadable ({status.error}) - " \
                    "possibly corrupted or incompletely transferred (seen on network drives)"
        return None, None

    names = rows.dtype.names or ()
    two_channels = "e(up)" in names
    has_irrep = "irrep(up)" in names if two_channels else "irrep" in names
    records = []
    for row in rows:
        if two_channels:
            up = {
                "energy": float(row["e(up)"]),
                "occupation": float(row["occ(up)"]),
                "localization": float(row["loc(up)"]),
                "irrep": str(row["irrep(up)"]) if has_irrep else None,
            }
            down = {
                "energy": float(row["e(down)"]),
                "occupation": float(row["occ(down)"]),
                "localization": float(row["loc(down)"]),
                "irrep": str(row["irrep(down)"]) if has_irrep else None,
            }
        else:
            up = {
                "energy": float(row["energy"]),
                "occupation": float(row["occ"]),
                "localization": float(row["loc"]),
                "irrep": str(row["irrep"]) if has_irrep else None,
            }
            down = {"energy": up["energy"], "occupation": 0.0, "localization": 0.0, "irrep": None}
        records.append({
            "band": int(row["nr"]) + 1,
            "up": up,
            "down": down,
        })
    return records, None


def load_vasp_output_payload(
    directory: str, band_start: int, band_end: int, include_orbitals: bool = True, irreps: bool = False,
    irrep_tol: float = 1e-1, symprec: float = 1e-3, include_ks=False, outcar_name="OUTCAR") -> dict:
    resolved = pathlib.Path(directory).resolve()
    if pathlib.Path(outcar_name).name != outcar_name:
        raise ValueError("Expected an OUTCAR filename within the calculation directory")
    output = None
    if include_orbitals:
        try:
            from puntukas.vasp import VaspOutput
        except ImportError as exc:
            print(json.dumps({"error": "puntukas_not_installed", "detail": str(exc)}), file=sys.stderr)
            raise
        output = VaspOutput.from_directory(str(resolved))
    elif outcar_name == "OUTCAR":
        try:
            from puntukas.vasp import VaspOutput
            output = VaspOutput.from_directory(str(resolved))
        except Exception:
            # Optional enrichment must not hide a usable OUTCAR when the XML reader fails.
            pass
    summary = _summary_payload(output, resolved, band_start, band_end, include_ks, outcar_name)
    gap = None
    if not include_orbitals and summary.get("ks_gap"):
        gap = summary["ks_gap"].copy()
        gap["bandgap"] = gap["lumo"] - gap["homo"]
    elif output is not None:
        try:
            gap = _band_gap_payload(output)
        except Exception:
            if include_orbitals:
                raise
    if include_orbitals:
        orbitals, orbitals_error = _orbitals_payload(output, band_start, band_end, irreps, irrep_tol, symprec)
    else:
        # Skips the WAVECAR read/per-band diagonalization entirely - CalculationSummaryPanel has
        # no use for orbital data and orbital_info is real per-band cost this
        # caller shouldn't pay just because it shares a bridge with ElectronicStructurePanel.
        orbitals, orbitals_error = None, None
    return {
        "path": str(resolved),
        "gap": gap,
        "orbitals": orbitals,
        "orbitals_error": orbitals_error,
        "summary": summary,
    }


def _selftest():
    from io import StringIO
    from types import SimpleNamespace
    from types import ModuleType
    from unittest.mock import patch
    files = {}
    with patch.object(pathlib.Path, "stat", return_value=SimpleNamespace(st_mtime=123.0)), \
         patch.object(pathlib.Path, "exists", return_value=True), \
         patch.object(pathlib.Path, "write_text", autospec=True,
                      side_effect=lambda p, text: files.update({p.name: text})), \
         patch.object(pathlib.Path, "open", autospec=True,
                      side_effect=lambda p, **kwargs: StringIO(files[p.name])):
        directory = pathlib.Path("selftest")
        path = directory / "OUTCAR"
        header = " ISPIN = 2 NSW = 40 IBRION = 2 EDIFFG = -0.02\n NBANDS= 3 NKPTS = 2 NIONS = 2\n"
        def ks(truncated=False):
            text = " E-fermi : 0.5000\n"
            for spin in (1, 2):
                text += f" spin component {spin}\n"
                for k in (1, 2):
                    text += f" k-point {k} : 0.0 0.0 0.0\n band No. band energies occupation\n"
                    text += " 1 -12.3456 1.00000\n 2 -1.0000 0.50000\n"
                    if truncated and spin == 2 and k == 2:
                        return text + " 3 1.0"
                    text += " 3 1.0000 0.00000\n\n"
            return text
        force = " TOTAL-FORCE (eV/Angst)\n -----\n 0 0 0 3 4 0\n 1 1 1 0 0 2\n -----\n"
        path.write_text(header + " Iteration 1( 1)\n free energy    TOTEN = -999 eV\n" +
            ks() + force + " free  energy   TOTEN = -10 eV\n" +
            " energy  without entropy= -9.9 energy(sigma->0) = -9.8\n" +
            " Iteration 2( 1)\n free  energy   TOTEN = -11 eV\n" + ks() +
            " Iteration 3( 1)\n free  energy   TOTEN = -12 eV\n" + ks(True))
        result = _scan_outcar(path, (2, 3))
        assert result["energy_trend"] == [-10, -11, -12]  # SCF -999 must never enter the trend.
        assert result["force_trend"] == [5.0, None, None]
        assert result["energy_without_entropy"] == [-9.9, None, None]
        assert result["energy_sigma_zero"] == [-9.8, None, None]
        assert result["nsw"] == 40 and result["ibrion"] == 2 and result["ediffg"] == -0.02
        assert [s["step"] for s in result["ks_steps"]] == [1, 2]
        assert result["ks_steps"][0]["fermi"] == 0.5
        for step in result["ks_steps"]:
            assert [(b["spin"], b["kpoint"]) for b in step["blocks"]] == [(1, 1), (1, 2), (2, 1), (2, 2)]
            for b in step["blocks"]:
                assert b["bands"] == [2, 3] and b["energies"] == [-1.0, 1.0]
                assert b["occupations"] == [0.5, 0.0]
        assert not result["finished"] and not result["reached_accuracy"]
        # No XML dependency for an OUTCAR-only summary, even with a broken XML alongside it.
        (directory / "vasprun.xml").write_text("<truncated")
        assert _summary_payload(None, directory, include_ks=True)["energy_trend"] == [-10, -11, -12]
        # The aggregate reader may reject a truncated XML before returning any output object.
        vasp = ModuleType("puntukas.vasp")
        class BrokenOutput:
            @staticmethod
            def from_directory(directory):
                raise SyntaxError("truncated vasprun.xml")
        vasp.VaspOutput = BrokenOutput
        with patch.dict(sys.modules, {"puntukas": ModuleType("puntukas"), "puntukas.vasp": vasp}):
            assert load_vasp_output_payload(str(directory), 2, 3, False, include_ks=True)["summary"]["energy_trend"] == [-10, -11, -12]
            class XmlOutput:
                @staticmethod
                def from_file(path):
                    return SimpleNamespace(energies=[[-99, -9], [-98, -8]], parameters={"ISPIN": 1})
            vasp.VasprunData = XmlOutput
            with patch.object(pathlib.Path, "exists", return_value=False):
                assert _summary_payload(None, directory)["energy_trend"] == [-9, -8]
        # ISPIN=1 has no spin header and occupations reach 2. Also test headerless step boundaries.
        path.write_text(" ISPIN = 1 NBANDS = 3 NKPTS = 1\n" +
            ks().split(" spin component 2")[0].replace(" spin component 1\n", "").split(" k-point 2")[0] +
            " free  energy   TOTEN = -1 eV\n" +
            ks().split(" spin component 2")[0].replace(" spin component 1\n", "").split(" k-point 2")[0] +
            " free  energy   TOTEN = -2 eV\n reached required accuracy\n General timing and accounting\n")
        result = _scan_outcar(path, (1, 3))
        assert result["energy_trend"] == [-1, -2] and len(result["ks_steps"]) == 2
        assert result["finished"] and result["reached_accuracy"]


def main() -> int:
    if sys.argv[1:] == ["--selftest"]:
        _selftest()
        print("OUTCAR summary selftest passed")
        return 0
    if len(sys.argv) < 2:
        raise SystemExit(
            "usage: vasp_output_load.py <calculation_directory> [band_start] [band_end] [include_orbitals] "
            "[irreps] [irrep_tol] [symprec] [include_ks] [outcar_name]")

    directory = sys.argv[1]
    band_start = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    band_end = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    include_orbitals = sys.argv[4] != "0" if len(sys.argv) > 4 else True
    irreps = sys.argv[5] != "0" if len(sys.argv) > 5 else False
    irrep_tol = float(sys.argv[6]) if len(sys.argv) > 6 else 1e-1
    symprec = float(sys.argv[7]) if len(sys.argv) > 7 else 1e-3

    include_ks = sys.argv[8] != "0" if len(sys.argv) > 8 else False
    outcar_name = sys.argv[9] if len(sys.argv) > 9 else "OUTCAR"
    payload = load_vasp_output_payload(directory, band_start, band_end, include_orbitals, irreps,
                                       irrep_tol, symprec, include_ks, outcar_name)
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
