"""Prepare portable cluster scripts from user assets; never submit or connect to a server."""
from __future__ import annotations

import argparse
import ast
from contextlib import redirect_stdout
import json
import math
import os
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True


def replace_nodes(text, replacements):
    lines = text.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    edits = []
    for node, value in replacements:
        # AST columns count UTF-8 bytes, not Unicode characters.
        start = offsets[node.lineno - 1] + len(lines[node.lineno - 1].encode()[:node.col_offset].decode())
        end = offsets[node.end_lineno - 1] + len(lines[node.end_lineno - 1].encode()[:node.end_col_offset].decode())
        edits.append((start, end, value))
    for start, end, value in sorted(edits, reverse=True):
        text = text[:start] + value + text[end:]
    return text


def configure(text, values, series=False):
    """Replace only module-level assignments with known values; retain all other bytes."""
    lines = text.splitlines()
    begin = next((i + 1 for i, line in enumerate(lines) if line.startswith("# ===== CONFIG =====")), None)
    end = next((i for i in range(begin or len(lines), len(lines))
                if lines[i].startswith("# ==================")), None)
    if begin is not None and end is None:
        raise ValueError("CONFIG block has no closing marker")
    replacements, names = [], []
    for node in ast.parse(text).body:
        if not isinstance(node, (ast.Assign, ast.AnnAssign)):
            continue
        targets = node.targets if isinstance(node, ast.Assign) else [node.target]
        in_block = begin is not None and begin < node.lineno <= end
        if not in_block and not series:
            continue
        # The real series asset uses tuple unpacking for the two bounds.
        if len(targets) == 1 and isinstance(targets[0], (ast.Tuple, ast.List)):
            unpacked = targets[0].elts
            if series and all(isinstance(t, ast.Name) and t.id in ("N_MIN", "N_MAX") for t in unpacked):
                replacements.append((node.value, ", ".join(values[t.id] for t in unpacked)))
                names.extend(t.id for t in unpacked)
            continue
        if len(targets) == 1 and isinstance(targets[0], ast.Name):
            name = targets[0].id
            if name in values and (in_block or name in ("N_MIN", "N_MAX")):
                replacements.append((node.value, values[name]))
                names.append(name)
    return replace_nodes(text, replacements), names


def adapt(text, filename, paths, shift, options, reference_n):
    """Narrow compatibility edits for non-CONFIG assumptions in older user assets."""
    if filename not in ("embeding.py", "hr_factors.py", "run_embeding_series.py"):
        return text, []
    tree = ast.parse(text)
    edits, changes = [], []
    for node in ast.walk(tree):
        if filename == "run_embeding_series.py" and node in tree.body and isinstance(node, ast.Assign):
            if any(isinstance(t, ast.Name) and t.id == "OUT_ROOT" for t in node.targets):
                edits.append((node.value, 'HERE / "embedded"'))
                changes.append("OUT_ROOT follows generated SYSTEM")
        if not isinstance(node, ast.Call):
            continue
        call = ast.unparse(node.func)
        if call == "ForceConstants" and node.args and isinstance(node.args[0], ast.BinOp):
            arg = node.args[0]
            if isinstance(arg.left, ast.Name) and arg.left.id == "SYSTEM" and isinstance(arg.right, ast.Constant):
                edits.append((arg, paths["FC_DIR"]))
                changes.append("defect FC path decoupled from output SYSTEM")
        if (filename == "hr_factors.py" and isinstance(node.func, ast.Attribute)
                and node.func.attr == "calculate_modes" and isinstance(node.func.value, ast.Name)
                and node.func.value.id == "fc"):
            statement = next((s for s in ast.walk(tree) if isinstance(s, ast.Assign) and s.value is node), None)
            if statement is not None:
                indent = " " * statement.col_offset
                prefix = (f"fc.atoms.set_positions(fc.atoms.get_positions() - {shift!r})\n{indent}"
                          f"fc.atoms.wrap()\n{indent}")
                edits.append((statement, prefix + ast.get_source_segment(text, statement)))
                changes.append("reference FC geometry aligned like GS/ES")
        if call in ("construct_and_embed_defect_mpi", "construct_and_embed_defect_serial"):
            for kw in node.keywords:
                if kw.arg == "unit_cell":
                    edits.append((kw.value, "bulk_fc.primitive_cell"))
                    changes.append("bulk primitive cell reused")
            # Shift only the in-memory FC geometry, exactly as the local embedding does.
            defect = ast.unparse(node.args[2])
            statement = next(s for s in tree.body if s.lineno <= node.lineno <= s.end_lineno)
            prefix = (f"{defect}.atoms.set_positions({defect}.atoms.get_positions() - {shift!r})\n"
                      f"{defect}.atoms.wrap()\n")
            original = ast.get_source_segment(text, statement)
            rewritten = original
            extra = []
            for key in ("dcutoff", "bcutoff"):
                value = getattr(options, key)
                if value is not None and not any(k.arg == key for k in node.keywords):
                    extra.append(f"{key}={value!r}")
            if extra:
                call_text = ast.get_source_segment(text, node)
                rewritten = rewritten.replace(call_text, call_text.rstrip()[:-1].rstrip().rstrip(",") + ", " + ", ".join(extra) + ")")
            # Apply nested unit-cell edits here rather than overlapping AST edits.
            for kw in node.keywords:
                if kw.arg == "unit_cell":
                    rewritten = rewritten.replace(ast.get_source_segment(text, kw), "unit_cell=bulk_fc.primitive_cell")
            edits = [(n, v) for n, v in edits if not (statement.lineno <= n.lineno <= statement.end_lineno)]
            edits.append((statement, prefix + rewritten))
            changes.append("local origin shift and embedding cutoffs")
        if call == "calc_hr_factors" and options.hr_method == "approximate_forces":
            if not any(k.arg == "unit_cell_phonons" for k in node.keywords):
                expr = ("ForceConstants(" + paths["BULK_PHONONS"] + ").calculate_modes()")
                original = ast.get_source_segment(text, node)
                edits.append((node, original.rstrip()[:-1].rstrip().rstrip(",") + f", unit_cell_phonons={expr})"))
                changes.append("approximate_forces receives bulk modes")
        if call == "calc_hr_factors" and options.hr_method == "forces" and not any(k.arg == "shift_defect" for k in node.keywords):
            original = ast.get_source_segment(text, node)
            edits.append((node, original.rstrip()[:-1].rstrip().rstrip(",") + f", shift_defect={[-s for s in shift]!r})"))
            changes.append("force geometry aligned in memory")
    if filename == "hr_factors.py" and reference_n != 9:
        for node in ast.walk(tree):
            if (isinstance(node, ast.Compare) and isinstance(node.left, ast.Name) and node.left.id == "n"
                    and len(node.comparators) == 1 and isinstance(node.comparators[0], ast.Constant)
                    and node.comparators[0].value == 9):
                edits.append((node.comparators[0], repr(reference_n)))
                changes.append("reference size follows defect cell")
    return replace_nodes(text, edits), sorted(set(changes))


def relative_input(path, output, role):
    try:
        relative = os.path.relpath(path, output).replace("\\", "/")
    except ValueError:
        relative = f"../cluster-inputs/{role}"
    return relative


def prepare_geometry(options):
    import numpy as np
    from dephonopy.force_constants import ForceConstants
    from dephonopy.hr_factors.io import read_atomic_displacements
    from phonon_vibronic_analysis import origin_shift, read_toten
    from zpl_formula import evaluate, used_variables

    fc = ForceConstants(str(options.fc_dir))
    bulk = ForceConstants(str(options.bulk_fc_dir))
    _, gs, es = read_atomic_displacements(str(options.gs_dir / "CONTCAR"),
                                           str(options.es_dir / "CONTCAR"), verbose=False)
    for geometry in (gs, es):
        if (geometry.get_chemical_symbols() != fc.atoms.get_chemical_symbols()
                or not np.allclose(geometry.get_cell(), fc.atoms.get_cell(), rtol=0, atol=1e-5)
                or not np.allclose(geometry.get_masses(), fc.atoms.get_masses(), rtol=0, atol=1e-5)):
            raise ValueError("FC, GS and ES must have the same ordered species, masses and cell")
        if not np.isfinite(geometry.get_positions()).all():
            raise ValueError("GS/ES positions must be finite")
    shift = origin_shift(fc.atoms, bulk.atoms, bulk.primitive_cell)
    for geometry in (gs, es):
        geometry.set_positions(geometry.get_positions() - shift)
        geometry.wrap()
    mult = np.asarray(fc.atoms.get_cell()) @ np.linalg.inv(bulk.primitive_cell.get_cell())
    reference_n = int(round(mult[0, 0]))
    if reference_n < 1:
        raise ValueError("Cannot determine defect supercell size")
    if options.zpl is not None:
        zpl = options.zpl
    else:
        formula = options.zpl_formula or "E_es - E_gs"
        sources = {"E_gs": options.gs_dir, "E_es": options.es_dir, "E_T": options.triplet_dir,
                   "E_gs@es": options.gs_at_es_dir, "E_es@gs": options.es_at_gs_dir}
        energies = {name: read_toten(sources[name]) if sources[name] else None for name in used_variables(formula)}
        zpl = 1000 * evaluate(formula, energies)
    if not math.isfinite(zpl) or zpl <= 0:
        raise ValueError("ZPL must be positive and finite")
    return (gs, es), shift.tolist(), zpl, reference_n


def generate(options):
    if not options.templates_dir.is_dir():
        raise ValueError(f"Templates directory does not exist: {options.templates_dir}")
    if options.n_min < 1 or options.n_max < options.n_min:
        raise ValueError("Expected 1 <= n-min <= n-max")
    if options.hr_method not in ("displacements", "approximate_forces", "forces"):
        raise ValueError("Unknown hr-method")
    for name in ("sigma", "gamma", "sigma_max", "hr_cutoff", "dcutoff", "bcutoff", "zpl"):
        value = getattr(options, name)
        if value is not None and (not math.isfinite(value) or value <= 0):
            raise ValueError(f"{name} must be positive and finite")
    if options.sigma_max is not None and options.sigma_max < options.sigma:
        raise ValueError("sigma-max must be >= sigma")
    if options.hr_method == "forces" and options.gs_at_es_dir is None:
        raise ValueError("forces requires --gs-at-es-dir")
    inputs = {"fc": options.fc_dir, "bulk": options.bulk_fc_dir, "gs": options.gs_dir, "es": options.es_dir}
    for role in ("triplet", "gs_at_es", "es_at_gs"):
        if getattr(options, role + "_dir") is not None:
            inputs[role] = getattr(options, role + "_dir")
    output = options.out_dir.absolute()
    for role, path in inputs.items():
        if not path.is_dir():
            raise ValueError(f"Input directory does not exist: {path}")
        if output.resolve() == path.resolve() or output.resolve() in path.resolve().parents:
            raise ValueError("Output must not contain an input directory")
    if output.is_symlink() or output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError("Choose a new or empty output directory; existing files will not be replaced")
    assets = list(options.templates_dir.rglob("*"))
    if any(p.is_symlink() for p in assets):
        raise ValueError("Template symlinks are not supported")
    required = ("embeding.py", "hr_factors.py", "lineshape.py", "run_embeding.job", "run.job", "run_embeding_series.py")
    for name in required:
        if not (options.templates_dir / name).is_file():
            raise ValueError(f"Missing template: {options.templates_dir / name}")
    if output.resolve() == options.templates_dir.resolve() or output.resolve() in options.templates_dir.resolve().parents:
        raise ValueError("Output must not contain templates")
    geometries, shift, zpl, reference_n = prepare_geometry(options)
    relative = {role: relative_input(path.resolve(), output.resolve(), role) for role, path in inputs.items()}
    path_expr = lambda role: "HERE / " + repr(relative[role])
    paths = {"FC_DIR": path_expr("fc"), "BULK_PHONONS": path_expr("bulk")}
    values = {**paths, "SYSTEM": "HERE", "INIT": 'HERE / "inputs/es/CONTCAR_aligned"',
              "FINAL": 'HERE / "inputs/gs/CONTCAR_aligned"', "METHOD": repr(options.hr_method),
              "SIZES": repr(list(range(options.n_min, options.n_max + 1))), "CUTOFFS": repr([options.hr_cutoff]),
              "SIG": repr(options.sigma), "SIG_MAX": repr(options.sigma_max), "GAMMA": repr(options.gamma),
              "ZPL": repr(zpl), "N_MIN": repr(options.n_min), "N_MAX": repr(options.n_max),
              "HR_DIR": repr(options.hr_method + (f"_cutoff{options.hr_cutoff:g}A" if options.hr_cutoff is not None else ""))}
    for name in ("dcutoff", "bcutoff"):
        if getattr(options, name) is not None:
            values[name.upper()] = repr(getattr(options, name))
    if options.hr_method == "forces":
        values["INIT"], values["FINAL"] = path_expr("es"), path_expr("gs_at_es")
    rendered, configured, deviations = {}, {}, {}
    for asset in assets:
        if not asset.is_file():
            continue
        name = asset.relative_to(options.templates_dir).as_posix()
        data = asset.read_bytes()
        if asset.suffix == ".py":
            text, configured[name] = configure(data.decode("utf-8"), values, name == "run_embeding_series.py")
            text, deviations[name] = adapt(text, name, paths, shift, options, reference_n)
            compile(text, name, "exec")
            if re.search(r"[A-Za-z]:[\\/]", text):
                raise ValueError(f"Absolute Windows path remains in template {name}; make the user asset portable")
            data = text.encode("utf-8")
        rendered[name] = data
    reserved = {"job.json", "README.txt", "inputs"}
    if any(Path(name).parts[0] in reserved for name in rendered):
        raise ValueError("Templates conflict with generated job.json, README.txt or inputs/")
    manifest = {"format": "ds.server_job.v1", "configured": configured, "compatibility_edits": deviations,
                "inputs": {role: {"source": str(inputs[role].resolve()), "relative": rel} for role, rel in relative.items()},
                "origin_shift_A": shift, "zpl_meV": zpl, "n_min": options.n_min, "n_max": options.n_max}
    readme = ("Copy this complete job folder to the cluster. Keep the input directories at the paths below,\n"
              "relative to this folder (copy them separately if needed). No input data is modified.\n\n")
    readme += "\n".join(f"{role}: {rel} <- {inputs[role].resolve()}" for role, rel in relative.items())
    readme += (f"\n\nFrom this job folder, after activating your scientific Python environment:\n"
               f"sbatch run_embeding.job {options.n_min}x{options.n_min}x1\n"
               "# Or run all sizes sequentially with the existing user series runner:\n"
               "nohup python3 run_embeding_series.py > series.log 2>&1 &\n"
               "# Wait for embedding to finish before HR, and for HR before lineshape:\n"
               "sbatch run.job hr_factors.py\npython lineshape.py\n\n"
               "Results: embedded/, spectral-function/, lineshape/ in this job folder; rescan it in DefectStudio.\n"
               "The application never submits jobs. Scheduler/environment settings remain user assets.\n"
               "Measured peak RAM: 10.6 GB at n=10, 30 GB at n=50, 36 MPI ranks (no prediction).\n"
               "Compatibility edits (if any) are listed in job.json; scientific code otherwise remains user supplied.\n")
    output.mkdir(parents=True, exist_ok=True)
    if output.is_symlink() or any(output.iterdir()):
        raise ValueError("Output is no longer empty; existing files will not be replaced")
    created_files, created_directories = [], []

    def make_directory(path):
        if path == output:
            return
        make_directory(path.parent)
        try:
            path.mkdir()
            created_directories.append(path)
        except FileExistsError:
            if path.is_symlink() or not path.is_dir():
                raise ValueError(f"Unsafe output subdirectory: {path}")

    def write_new(path, data):
        make_directory(path.parent)
        with path.open("xb") as handle:
            created_files.append(path)
            handle.write(data)

    try:
        write_new(output / ".server-job.lock", b"")
        for name, data in rendered.items():
            write_new(output / name, data)
        for role, atoms in zip(("gs", "es"), geometries):
            target = output / "inputs" / role / "CONTCAR_aligned"
            write_new(target, b"")
            atoms.write(str(target), format="vasp")
        write_new(output / "README.txt", readme.encode("utf-8"))
        write_new(output / "job.json", (json.dumps(manifest, indent=2) + "\n").encode("utf-8"))
        (output / ".server-job.lock").unlink()
    except BaseException:
        # Never remove another process's files, including when the destination races us.
        for child in reversed(created_files):
            child.unlink(missing_ok=True)
        for child in reversed(created_directories):
            try:
                child.rmdir()
            except OSError:
                pass
        raise
    return output


def main():
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise ValueError(message)

    try:
        parser = Parser(description=__doc__)
        for flag in ("templates", "fc", "bulk-fc", "gs", "es", "out"):
            parser.add_argument(f"--{flag}-dir", type=Path, required=True)
        for flag in ("triplet", "gs-at-es", "es-at-gs"):
            parser.add_argument(f"--{flag}-dir", type=Path)
        parser.add_argument("--n-min", type=int, required=True)
        parser.add_argument("--n-max", type=int, required=True)
        parser.add_argument("--hr-method", choices=("displacements", "approximate_forces", "forces"), default="displacements")
        for flag, default in (("sigma", 6.0), ("gamma", 2.0), ("sigma-max", None), ("hr-cutoff", None),
                              ("dcutoff", None), ("bcutoff", None), ("zpl", None)):
            parser.add_argument(f"--{flag}", type=float, default=default)
        parser.add_argument("--zpl-formula")
        parser.add_argument("--label", default="")
        parser.add_argument("--fallback-dir", type=Path)
        with redirect_stdout(sys.stderr):
            options = parser.parse_args()
            try:
                output = generate(options)
            except PermissionError:
                if options.fallback_dir is None:
                    raise
                options.out_dir = options.fallback_dir
                output = generate(options)
        print(json.dumps({"ok": True, "out_dir": str(output)}))
        return 0
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        return 1


if __name__ == "__main__":
    sys.exit(main())
