from __future__ import annotations

import argparse
import sys

from scripts.python.common.cli import print_header, print_step
from scripts.python.common.exec import run_command
from scripts.python.common.paths import repo_root
from scripts.python.common.preferences import load_preferences, resolve_preference
from scripts.python.common.tooling import detect_tool


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build project.")
    parser.add_argument("--config", choices=["Debug", "Release", "Dist"], default=None)
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default=None)
    # NOTE: target, solution path, and project name are currently hardcoded to "Hazelnut" / "Hazel"
    # These are placeholders for future project refactoring when the actual application structure is finalized.
    parser.add_argument("--target", default=None)
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--rebuild", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def run_msvc(args: argparse.Namespace) -> int:
    msbuild = detect_tool("msbuild")
    if not msbuild:
        print("[error] MSBuild not found.")
        return 1

    # NOTE: Solution path is currently hardcoded. This is a placeholder.
    solution = repo_root() / "Hazel" / "Hazel.sln"
    if not solution.exists():
        print(f"[error] Solution not found: {solution}")
        return 1

    command = [
        msbuild,
        str(solution),
        f"/p:Configuration={args.config}",
        "/m",
    ]
    if args.rebuild:
        command.append("/t:Rebuild")
    else:
        command.append("/t:Build")

    return run_command(
        command, cwd=repo_root(), dry_run=args.dry_run, verbose=args.verbose
    )


def run_make(args: argparse.Namespace) -> int:
    make = detect_tool("make")
    if not make:
        print("[error] make not found.")
        return 1

    command = [make, f"config={args.config}"]
    if args.target:
        command.append(args.target)
    if args.clean:
        command.append("clean")

    return run_command(
        command, cwd=repo_root() / "Hazel", dry_run=args.dry_run, verbose=args.verbose
    )


def run(args: argparse.Namespace) -> int:
    preferences = load_preferences().get("build", {})
    args.config = resolve_preference(preferences, "config", args.config, "Debug")
    args.compiler = resolve_preference(preferences, "compiler", args.compiler, "msvc")
    args.target = resolve_preference(preferences, "target", args.target, "Hazelnut")

    print_header("Build")
    print_step(f"config={args.config} compiler={args.compiler} target={args.target}")
    if args.compiler == "msvc":
        return run_msvc(args)
    return run_make(args)


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
