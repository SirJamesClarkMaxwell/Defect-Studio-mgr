from __future__ import annotations

import argparse
import sys

from scripts.python.common.cli import print_header, print_step
from scripts.python.common.exec import run_command
from scripts.python.common.paths import repo_root
from scripts.python.common.tooling import detect_tool


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Export Python environment snapshots.")
    parser.add_argument("--check-lock", action="store_true")
    parser.add_argument("--update-lock", action="store_true")
    parser.add_argument("--group", action="append", default=[])
    parser.add_argument("--all-groups", action="store_true")
    parser.add_argument("--requirements-out", default="scripts/requirements")
    parser.add_argument("--freeze-installed", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def run(args: argparse.Namespace) -> int:
    uv = detect_tool("uv")
    if not uv:
        print("[error] uv not found.")
        return 1

    print_header("Export Python Environment")
    project_root = repo_root()

    if args.check_lock:
        print_step("Checking lock")
        code = run_command([uv, "lock", "--check"], cwd=project_root, dry_run=args.dry_run, verbose=args.verbose)
        if code != 0:
            return code

    if args.update_lock:
        print_step("Updating lock")
        code = run_command([uv, "lock"], cwd=project_root, dry_run=args.dry_run, verbose=args.verbose)
        if code != 0:
            return code

    out_dir = project_root / args.requirements_out
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.freeze_installed:
        print_step("Exporting full installed snapshot")
        code = run_command(
            [uv, "pip", "freeze", "--quiet"],
            cwd=project_root,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
        if code != 0:
            return code

    groups = args.group
    if args.all_groups:
        groups = ["dev", "scientific-core", "scientific-extended", "docs"]

    for group in groups:
        out_file = out_dir / f"requirements-{group}.txt"
        print_step(f"Exporting group {group} -> {out_file}")
        code = run_command(
            [uv, "export", "--group", group, "--format", "requirements-txt", "--output-file", str(out_file)],
            cwd=project_root,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
        if code != 0:
            return code

    return 0


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
