from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

from scripts.python.common.cli import print_header, print_step
from scripts.python.common.exec import run_command
from scripts.python.common.paths import (
    app_executable_path,
    repo_root,
    tracy_profiler_path,
)
from scripts.python.common.preferences import load_preferences, resolve_preference


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run the app and automatically launch Tracy profiler (Windows only)."
    )
    parser.add_argument("--config", choices=["Debug", "Release", "Dist"], default=None)
    parser.add_argument(
        "--project", default=None, help="Optional project path to pass to app."
    )
    parser.add_argument(
        "--log-level", choices=["trace", "debug", "info", "warn", "error"], default=None
    )
    parser.add_argument(
        "--log-file",
        nargs="?",
        const="",
        default=None,
        help="Enable file logging and optionally set the log file path.",
    )
    parser.add_argument("--safe-mode", action="store_true", default=None)
    parser.add_argument("--reset-layout", action="store_true", default=None)
    parser.add_argument("--exe", help="Explicit executable path override.")
    parser.add_argument(
        "--profiler",
        help="Explicit Tracy profiler executable path override.",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def build_app_command(args: argparse.Namespace, exe_path: Path) -> list[str]:
    command = [str(exe_path), f"--log-level={args.log_level}"]
    if args.log_file is not None:
        command.append(
            "--log-file" if args.log_file == "" else f"--log-file={args.log_file}"
        )
    if args.safe_mode:
        command.append("--safe-mode")
    if args.reset_layout:
        command.append("--reset-layout")
    if args.project:
        command.append(f"--project={args.project}")
    return command


def run(args: argparse.Namespace) -> int:
    if os.name != "nt":
        print("[error] run_with_tracy is currently supported only on Windows.")
        return 1

    preferences = load_preferences().get("run", {})
    args.config = resolve_preference(preferences, "config", args.config, "Debug")
    args.project = resolve_preference(preferences, "project", args.project, None)
    args.log_level = resolve_preference(
        preferences, "log_level", args.log_level, "info"
    )
    args.safe_mode = resolve_preference(preferences, "safe_mode", args.safe_mode, False)
    args.reset_layout = resolve_preference(
        preferences, "reset_layout", args.reset_layout, False
    )

    print_header("Run With Tracy")

    profiler_path = Path(args.profiler) if args.profiler else tracy_profiler_path()
    if not profiler_path.exists() and not args.dry_run:
        print(f"[error] Tracy profiler not found: {profiler_path}")
        return 1

    exe_path = Path(args.exe) if args.exe else app_executable_path(args.config)
    if not exe_path.exists() and not args.dry_run:
        print(f"[error] executable not found: {exe_path}")
        return 1

    print_step(f"launch_profiler={profiler_path}")
    if not args.dry_run:
        subprocess.Popen([str(profiler_path)], cwd=str(repo_root()))

    command = build_app_command(args, exe_path)
    print_step(f"launch_app={exe_path}")
    return run_command(
        command, cwd=repo_root(), dry_run=args.dry_run, verbose=args.verbose
    )


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
