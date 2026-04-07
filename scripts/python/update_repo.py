from __future__ import annotations

import argparse
import subprocess
import sys

from scripts.python.common.cli import print_header, print_step
from scripts.python.common.exec import run_command
from scripts.python.common.paths import repo_root


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Update the repository from the remote origin."
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def run(args: argparse.Namespace) -> int:
    print_header("Update Repository")
    status = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=str(repo_root()),
        check=False,
        capture_output=True,
        text=True,
    )
    if status.returncode != 0:
        print("[error] Unable to inspect repository status.")
        return status.returncode
    if status.stdout.strip():
        print(
            "[error] Repository has local changes. Commit or stash them before updating."
        )
        return 1

    print_step("Running git pull --ff-only")
    return run_command(
        ["git", "pull", "--ff-only"],
        cwd=repo_root(),
        dry_run=args.dry_run,
        verbose=args.verbose,
    )


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
