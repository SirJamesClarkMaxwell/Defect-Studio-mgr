from __future__ import annotations

import argparse
import sys

import build as build_script
import generate_projects as generate_script
import run as run_script


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local CI checks.")
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default="msvc")
    parser.add_argument("--generator", choices=["vs2022", "gmake2"], default="vs2022")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def run(args: argparse.Namespace) -> int:
    print("== CI Check ==")

    code = generate_script.run(
        argparse.Namespace(
            generator=args.generator,
            compiler=args.compiler,
            clean=False,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
    )
    if code != 0:
        return code

    for cfg in ["Debug", "Release"]:
        code = build_script.run(
            argparse.Namespace(
                config=cfg,
                compiler=args.compiler,
                target="Hazelnut",
                clean=False,
                rebuild=False,
                dry_run=args.dry_run,
                verbose=args.verbose,
            )
        )
        if code != 0:
            return code

    return run_script.run(
        argparse.Namespace(
            config="Debug",
            project=None,
            log_level="info",
            safe_mode=False,
            reset_layout=False,
            exe=None,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
    )


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
