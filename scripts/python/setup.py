from __future__ import annotations

import argparse
import platform
import sys

import build as build_script
import doctor as doctor_script
import generate_projects as generate_script
import run as run_script
from scripts.python.common.cli import add_common_flags, print_header, print_step
from scripts.python.common.config import save_local_config
from scripts.python.common.exec import run_command
from scripts.python.common.paths import repo_root
from scripts.python.common.preferences import load_preferences, resolve_preference
from scripts.python.common.tooling import (
    detect_many,
    detect_msvc_compiler_path,
    detect_msvc_installation_path,
)


PHASES = [
    "repo-update",
    "git-submodule",
    "tool-discovery",
    "python-env",
    "vendor-check",
    "generate",
    "build",
    "open",
    "run",
]


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Bootstrap Defect Studio development environment."
    )
    add_common_flags(parser)

    parser.add_argument(
        "--update-repo",
        action="store_true",
        default=None,
        help="Run git pull --ff-only before the rest of setup.",
    )
    parser.add_argument(
        "--install-tools",
        action="store_true",
        help="Reserved for managed tool installation.",
    )
    parser.add_argument("--python-group", action="append", default=None)
    parser.add_argument(
        "--python-version",
        default=None,
        help="Target Python version prepared by bootstrap wrappers (e.g. 3.12).",
    )
    parser.add_argument("--generator", choices=["vs2022", "gmake2"], default=None)
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default=None)
    parser.add_argument("--config", choices=["Debug", "Release", "Dist"], default=None)
    parser.add_argument("--open", choices=["vs", "vscode", "none"], default=None)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--phase", choices=PHASES, action="append", default=[])
    return parser


def should_run(phases: list[str], name: str) -> bool:
    return not phases or name in phases


def run_repo_update(args: argparse.Namespace) -> int:
    if not args.update_repo:
        print_step("Skipping repository update")
        return 0
    print_step("Updating repository")
    return run_command(
        ["git", "pull", "--ff-only"],
        cwd=repo_root(),
        dry_run=args.dry_run,
        verbose=args.verbose,
    )


def run_git_submodule(args: argparse.Namespace) -> int:
    print_step("Initializing and updating git submodules")
    code = run_command(
        ["git", "submodule", "init"],
        cwd=repo_root(),
        dry_run=args.dry_run,
        verbose=args.verbose,
    )
    if code != 0:
        return code
    return run_command(
        ["git", "submodule", "update"],
        cwd=repo_root(),
        dry_run=args.dry_run,
        verbose=args.verbose,
    )


def run_tool_discovery(args: argparse.Namespace) -> int:
    print_step("Discovering tools")
    preferences = load_preferences().get("setup", {})
    tools = detect_many(
        [
            "msbuild",
            "make",
            "gcc",
            "clang",
        ]
    )

    msvc_installation_path = detect_msvc_installation_path()
    msvc_compiler_path = detect_msvc_compiler_path()
    compiler_choice = resolve_preference(preferences, "compiler", args.compiler, "msvc")
    if compiler_choice == "msvc" and msvc_compiler_path is None:
        print("[error] Visual Studio/MSVC was not found.")
        print("        On Windows, Visual Studio 2022 or Build Tools are recommended.")
        print(
            "        Use the Visual Studio Installer to add the MSVC toolchain and MSBuild components."
        )
        return 1
    if msvc_installation_path:
        tools["msvc-installation"] = msvc_installation_path
    if msvc_compiler_path:
        tools["msvc-cl"] = msvc_compiler_path
    for name, path in tools.items():
        status = path if path else "MISSING"
        print(f"- {name:8} {status}")

    save_local_config(
        {
            "platform": platform.platform(),
            "tools": tools,
            "python_groups": args.python_group,
        }
    )
    return 0


def run_python_env(args: argparse.Namespace) -> int:
    version_note = (
        f" (target Python {args.python_version})" if args.python_version else ""
    )
    print_step(f"Preparing Python env{version_note}")
    preferences = load_preferences().get("setup", {})
    groups = args.python_group or preferences.get("python_groups") or ["dev"]
    uv = detect_many(["uv"]).get("uv")
    if not uv:
        print(
            "[error] uv not found. Run setup via scripts/Windows/Setup.bat or scripts/Linux/Setup.sh bootstrap wrappers."
        )
        return 1

    base_cmd = [uv, "sync"]
    for group in groups:
        base_cmd.extend(["--group", group])

    return run_command(
        base_cmd, cwd=repo_root(), dry_run=args.dry_run, verbose=args.verbose
    )


def run_vendor_check(args: argparse.Namespace) -> int:
    print_step("Vendor/dependency check placeholder")
    if args.install_tools:
        print_step(
            "Managed install requested (placeholder, no auto-download in this revision)."
        )
    return 0


def run_generate(args: argparse.Namespace) -> int:
    preferences = load_preferences().get("setup", {})
    return generate_script.run(
        argparse.Namespace(
            generator=resolve_preference(
                preferences, "generator", args.generator, "vs2022"
            ),
            compiler=resolve_preference(preferences, "compiler", args.compiler, "msvc"),
            clean=False,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
    )


def run_build(args: argparse.Namespace) -> int:
    preferences = load_preferences().get("setup", {})
    return build_script.run(
        argparse.Namespace(
            config=resolve_preference(preferences, "config", args.config, "Debug"),
            compiler=resolve_preference(preferences, "compiler", args.compiler, "msvc"),
            target=resolve_preference(preferences, "target", None, None),
            clean=False,
            rebuild=False,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
    )


def run_open(args: argparse.Namespace) -> int:
    preferences = load_preferences().get("setup", {})
    open_mode = resolve_preference(preferences, "open", args.open, "none")
    if open_mode == "none":
        print_step("Skipping IDE open")
        return 0
    if open_mode == "vscode":
        return run_command(
            ["code", str(repo_root())],
            cwd=repo_root(),
            dry_run=args.dry_run,
            verbose=args.verbose,
        )

    sln = repo_root() / "Hazel" / "Hazel.sln"
    return run_command(
        ["cmd", "/c", "start", "", str(sln)],
        cwd=repo_root(),
        dry_run=args.dry_run,
        verbose=args.verbose,
    )


def run_app(args: argparse.Namespace) -> int:
    preferences = load_preferences().get("setup", {})
    return run_script.run(
        argparse.Namespace(
            config=resolve_preference(preferences, "config", args.config, "Debug"),
            project=None,
            log_level="info",
            safe_mode=False,
            reset_layout=False,
            exe=None,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
    )


def run(args: argparse.Namespace) -> int:
    print_header("Setup")
    preferences = load_preferences().get("setup", {})
    args.update_repo = resolve_preference(
        preferences, "update_repo", args.update_repo, False
    )
    args.compiler = resolve_preference(preferences, "compiler", args.compiler, "msvc")
    args.generator = resolve_preference(
        preferences, "generator", args.generator, "vs2022"
    )
    args.config = resolve_preference(preferences, "config", args.config, "Debug")
    args.open = resolve_preference(preferences, "open", args.open, "none")
    args.python_group = args.python_group or preferences.get("python_groups") or ["dev"]

    if args.python_version:
        print_step(f"Bootstrap provided Python target: {args.python_version}")

    if should_run(args.phase, "repo-update"):
        code = run_repo_update(args)
        if code != 0:
            return code
    if should_run(args.phase, "git-submodule"):
        code = run_git_submodule(args)
        if code != 0:
            return code
    if should_run(args.phase, "tool-discovery"):
        code = run_tool_discovery(args)
        if code != 0:
            return code
    if should_run(args.phase, "python-env"):
        code = run_python_env(args)
        if code != 0:
            return code
    if should_run(args.phase, "vendor-check"):
        code = run_vendor_check(args)
        if code != 0:
            return code
    if should_run(args.phase, "generate"):
        code = run_generate(args)
        if code != 0:
            return code
    if should_run(args.phase, "build"):
        code = run_build(args)
        if code != 0:
            return code
    if should_run(args.phase, "open"):
        code = run_open(args)
        if code != 0:
            return code
    if args.run and should_run(args.phase, "run"):
        code = run_app(args)
        if code != 0:
            return code

    print("\nSetup completed.")
    return doctor_script.run(argparse.Namespace(verbose=False))


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
