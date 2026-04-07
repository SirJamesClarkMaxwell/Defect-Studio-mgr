from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

import build as build_script
import generate_projects as generate_script
import run as run_script
from scripts.python.common.paths import project_name
from scripts.python.common.preferences import load_preferences, resolve_preference


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Regenerate, build and run.")
    parser.add_argument("--regenerate", action="store_true", default=None)
    parser.add_argument("--generator", choices=["vs2022", "gmake2"], default=None)
    parser.add_argument("--config", choices=["Debug", "Release", "Dist"], default=None)
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default=None)
    parser.add_argument("--project", default=None)
    parser.add_argument("--target", default=None)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def run(args: argparse.Namespace) -> int:
    build_preferences = load_preferences().get("build", {})
    run_preferences = load_preferences().get("run", {})
    args.generator = resolve_preference(
        build_preferences, "generator", args.generator, "vs2022"
    )
    args.config = resolve_preference(build_preferences, "config", args.config, "Debug")
    args.compiler = resolve_preference(
        build_preferences, "compiler", args.compiler, "msvc"
    )
    args.target = resolve_preference(
        build_preferences, "target", args.target, project_name()
    )
    args.project = resolve_preference(run_preferences, "project", args.project, None)

    if args.regenerate:
        generate_args = argparse.Namespace(
            generator=args.generator,
            compiler=args.compiler,
            clean=False,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
        code = generate_script.run(generate_args)
        if code != 0:
            return code

    build_args = argparse.Namespace(
        config=args.config,
        compiler=args.compiler,
        target=args.target,
        clean=False,
        rebuild=False,
        dry_run=args.dry_run,
        verbose=args.verbose,
    )
    code = build_script.run(build_args)
    if code != 0:
        return code

    run_args = argparse.Namespace(
        config=args.config,
        project=args.project,
        log_level="info",
        safe_mode=False,
        reset_layout=False,
        exe=None,
        dry_run=args.dry_run,
        verbose=args.verbose,
    )
    return run_script.run(run_args)


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
