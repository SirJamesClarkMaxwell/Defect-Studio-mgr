from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Tuple

THIS_FILE = Path(__file__).resolve()
REPO_ROOT = THIS_FILE.parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.python.common.paths import repo_root, test_executable_path


def resolve_test_executable(config: str, compiler: str) -> Path:
    default_path = test_executable_path(config)
    if (
        default_path.exists()
        or compiler == "msvc"
        or not sys.platform.startswith("win")
    ):
        return default_path

    name = "DefectStudioTests.exe"
    candidate = (
        repo_root()
        / "build"
        / "bin"
        / f"{config}-windows-x86_64-gmake2"
        / "DefectStudioTests"
        / name
    )
    if candidate.exists():
        return candidate

    return default_path


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run test matrix across compilers for Release and Dist."
    )
    parser.add_argument(
        "--compiler",
        "--compilers",
        "-Compilers",
        dest="compilers",
        action="append",
        default=[],
    )
    parser.add_argument("--continue-on-error", "-ContinueOnError", action="store_true")
    parser.add_argument("--verbose", "-VerboseBuild", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def default_compilers() -> List[str]:
    if sys.platform.startswith("win"):
        return ["msvc", "gcc", "clang"]
    return ["gcc", "clang"]


def generator_for_compiler(compiler: str) -> str:
    if compiler == "msvc" and not sys.platform.startswith("win"):
        raise ValueError("msvc is available only on Windows")
    if compiler == "msvc":
        return "vs2022"
    return "gmake2"


def run_step(title: str, command: List[str], dry_run: bool) -> int:
    print(f"\n==> {title}")
    print(f"[cmd] {' '.join(command)}")
    if dry_run:
        return 0
    env = os.environ.copy()
    package_root = str(repo_root())
    existing_pythonpath = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = (
        f"{package_root}{os.pathsep}{existing_pythonpath}"
        if existing_pythonpath
        else package_root
    )
    result = subprocess.run(command, cwd=str(repo_root()), check=False, env=env)
    return result.returncode


def clean_build_artifacts(config: str, dry_run: bool, verbose: bool) -> None:
    build_root = repo_root() / "build"
    targets = [
        build_root / "bin",
        build_root / "bin-int",
        repo_root() / "Vendor" / "yaml-cpp" / "build" / "bin",
        repo_root() / "Vendor" / "yaml-cpp" / "build" / "bin-int",
    ]
    prefix = f"{config}-"

    for base in targets:
        if not base.exists():
            continue
        for child in base.iterdir():
            if not child.is_dir() or not child.name.startswith(prefix):
                continue
            if verbose or dry_run:
                print(f"[clean] remove {child}")
            if not dry_run:
                shutil.rmtree(child, ignore_errors=True)


def run(args: argparse.Namespace) -> int:
    compilers = args.compilers or default_compilers()
    configs = ["Release", "Dist"]

    for compiler in compilers:
        if compiler not in {"msvc", "gcc", "clang"}:
            print(f"[error] Unsupported compiler: {compiler}")
            return 2

    python_exe = sys.executable
    generate_script = str(repo_root() / "scripts" / "python" / "generate_projects.py")
    build_script = str(repo_root() / "scripts" / "python" / "build.py")

    rows: List[Tuple[str, str, str, int, str]] = []
    failed = False

    for compiler in compilers:
        generator = generator_for_compiler(compiler)
        generate_cmd = [
            python_exe,
            generate_script,
            "--generator",
            generator,
            "--compiler",
            compiler,
        ]
        if args.verbose:
            generate_cmd.append("--verbose")

        code = run_step(
            f"Generate ({compiler} / {generator})", generate_cmd, args.dry_run
        )
        rows.append(("generate", compiler, "-", code, "PASS" if code == 0 else "FAIL"))
        if code != 0:
            failed = True
            if not args.continue_on_error:
                break
            continue

        for config in configs:
            clean_build_artifacts(config, args.dry_run, args.verbose)

            build_cmd = [
                python_exe,
                build_script,
                "--compiler",
                compiler,
                "--config",
                config,
                "--target",
                "DefectStudioTests",
            ]
            if args.verbose:
                build_cmd.append("--verbose")

            code = run_step(
                f"Build Tests ({compiler} / {config})", build_cmd, args.dry_run
            )
            rows.append(
                ("build", compiler, config, code, "PASS" if code == 0 else "FAIL")
            )
            if code != 0:
                failed = True
                if not args.continue_on_error:
                    break
                continue

            test_exe = resolve_test_executable(config, compiler)
            if not test_exe.exists() and not args.dry_run:
                print(f"[error] test executable not found: {test_exe}")
                code = 1
            else:
                run_cmd = [str(test_exe)]
                code = run_step(
                    f"Run Tests ({compiler} / {config})", run_cmd, args.dry_run
                )

            rows.append(
                ("test", compiler, config, code, "PASS" if code == 0 else "FAIL")
            )
            if code != 0:
                failed = True
                if not args.continue_on_error:
                    break

        if failed and not args.continue_on_error:
            break

    print("\n=== Test Matrix Summary ===")
    print(f"{'Phase':8} {'Compiler':8} {'Config':8} {'Exit':8} {'Status':6}")
    for phase, compiler, config, code, status in rows:
        print(f"{phase:8} {compiler:8} {config:8} {code:<8} {status:6}")

    if failed:
        print("\nTest matrix finished with failures.")
        return 1

    print("\nTest matrix finished successfully.")
    return 0


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
