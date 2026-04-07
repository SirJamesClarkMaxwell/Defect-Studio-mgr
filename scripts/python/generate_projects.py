from __future__ import annotations

import argparse
import sys

from scripts.python.common.cli import print_header, print_step
from scripts.python.common.exec import run_command
from scripts.python.common.paths import repo_root
from scripts.python.common.preferences import load_preferences, resolve_preference
from scripts.python.common.tooling import detect_tool


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate project files.")
    parser.add_argument("--generator", choices=["vs2022", "gmake2"], default=None)
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default=None)
    parser.add_argument(
        "--clean", action="store_true", help="Reserved for generated files cleanup."
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def run(args: argparse.Namespace) -> int:
    preferences = load_preferences().get("build", {})
    args.generator = resolve_preference(
        preferences, "generator", args.generator, "vs2022"
    )
    args.compiler = resolve_preference(preferences, "compiler", args.compiler, "msvc")

    premake = detect_tool("premake5")
    if not premake:
        print("[error] premake5 not found in PATH or tools/premake.")
        return 1

    print_header("Generate Projects")
    if args.clean:
        print_step(
            "Clean requested (current version keeps this as a no-op placeholder)."
        )

    command = [premake, "vs2022" if args.generator == "vs2022" else "gmake2"]
    if args.compiler == "clang":
        command.append("--cc=clang")
    elif args.compiler == "gcc":
        command.append("--cc=gcc")

    return run_command(
        command, cwd=repo_root(), dry_run=args.dry_run, verbose=args.verbose
    )


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
