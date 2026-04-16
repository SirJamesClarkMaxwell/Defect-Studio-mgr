from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from scripts.python.common.cli import print_header, print_step
from scripts.python.common.exec import run_command
from scripts.python.common.paths import project_name, repo_root, solution_path
from scripts.python.common.preferences import load_preferences, resolve_preference
from scripts.python.common.tooling import detect_tool


def build_job_count(reserve: int = 2) -> int:
    cpu_count = os.cpu_count() or 1
    return max(cpu_count - reserve, 1)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build project.")
    parser.add_argument("--config", choices=["Debug", "Release", "Dist"], default=None)
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default=None)
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

    solution = solution_path("vs2022")
    if not solution.exists():
        print(f"[error] Solution not found: {solution}")
        return 1

    jobs = build_job_count()
    if args.verbose:
        print(f"[step] Using /maxcpucount:{jobs} on MSBuild")

    command = [
        msbuild,
        str(solution),
        f"/p:Configuration={args.config}",
        f"/maxcpucount:{jobs}",
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
        if os.name != "nt":
            print("        Install required build tools, e.g.:")
            print("        sudo apt update && sudo apt install -y build-essential")
        return 1

    if os.name != "nt":
        required_headers = {
            "X11/Xlib.h": "libx11-dev",
            "X11/extensions/Xrandr.h": "libxrandr-dev",
            "X11/extensions/Xinerama.h": "libxinerama-dev",
            "X11/Xcursor/Xcursor.h": "libxcursor-dev",
            "X11/extensions/XInput2.h": "libxi-dev",
            "X11/extensions/xf86vmode.h": "libxxf86vm-dev",
            "EGL/egl.h": "libegl1-mesa-dev",
            "GL/gl.h": "libgl1-mesa-dev",
        }

        include_roots = [Path("/usr/include"), Path("/usr/local/include")]
        missing: list[tuple[str, str]] = []
        for header, package in required_headers.items():
            if not any((root / header).exists() for root in include_roots):
                missing.append((header, package))

        if missing:
            print(
                "[error] Missing Linux development headers required by GLFW (X11 backend)."
            )
            for header, package in missing:
                print(f"        - {header}  (package: {package})")
            print("        Install prerequisites:")
            print("        sudo apt update")
            print(
                "        sudo apt install -y build-essential pkg-config "
                "libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxxf86vm-dev "
                "libgl1-mesa-dev libegl1-mesa-dev"
            )
            return 1

    make_dir = repo_root() / "build" / "generated" / "gmake2"
    if not make_dir.exists() and not args.dry_run:
        print(f"[error] Make build directory not found: {make_dir}")
        return 1

    command = [make, f"config={args.config.lower()}"]
    if os.name == "nt":
        comspec = os.environ.get("ComSpec") or os.environ.get("COMSPEC")
        if comspec:
            command.append(f"ComSpec={comspec}")
            command.append(f"SHELL={comspec}")
    if args.target:
        command.append(args.target)
    if args.clean:
        command.append("clean")

    env_override: dict[str, str] = {}
    if args.compiler == "gcc":
        gcc = detect_tool("gcc")
        gxx = shutil.which("g++")
        if not gxx and gcc:
            candidate = Path(gcc).with_name("g++.exe")
            if candidate.exists():
                gxx = str(candidate)
        if not gcc or not gxx:
            print("[error] gcc/g++ toolchain not found.")
            return 1
        env_override["CC"] = gcc
        env_override["CXX"] = gxx
    elif args.compiler == "clang":
        clang = detect_tool("clang")
        clangxx = shutil.which("clang++")
        if not clangxx and clang:
            candidate = Path(clang).with_name("clang++.exe")
            if candidate.exists():
                clangxx = str(candidate)
        if not clang or not clangxx:
            print("[error] clang/clang++ toolchain not found.")
            print(f"        clang:   {clang or 'MISSING'}")
            print(f"        clang++: {clangxx or 'MISSING'}")
            if os.name == "nt":
                print(
                    "        Install LLVM/Clang and ensure both clang.exe and clang++.exe are in PATH."
                )
                print("        Example: winget install LLVM.LLVM")
                print(
                    "        Then restart the terminal and rerun TestBuildMatrix.bat."
                )
            else:
                print(
                    "        Install clang and clang++ packages and ensure they are in PATH."
                )
            return 1
        env_override["CC"] = clang
        env_override["CXX"] = clangxx

    if args.verbose and env_override:
        print(
            f"[step] toolchain CC={env_override.get('CC')} CXX={env_override.get('CXX')}"
        )

    on_vmware_shared_folder = False
    if os.name != "nt":
        root_path = str(repo_root()).replace("\\", "/")
        on_vmware_shared_folder = "/mnt/hgfs/" in root_path or root_path.endswith(
            "/mnt/hgfs"
        )
        if on_vmware_shared_folder:
            print(
                "[warn] Repository is located on a VMware shared folder (/mnt/hgfs). "
                "GNU make may report timestamps in the future and rebuilds can become unstable. "
                "Use a normal Linux filesystem path for builds if possible."
            )

    jobs = build_job_count()
    if on_vmware_shared_folder:
        jobs = 1
        if args.verbose:
            print(
                "[step] Using -j1 on /mnt/hgfs to avoid make timestamp/LTO instability."
            )
    command.insert(1, f"-j{jobs}")

    return run_command(
        command,
        cwd=make_dir,
        dry_run=args.dry_run,
        verbose=args.verbose,
        env=env_override or None,
    )


def run(args: argparse.Namespace) -> int:
    preferences = load_preferences().get("build", {})
    args.config = resolve_preference(preferences, "config", args.config, "Debug")
    args.compiler = resolve_preference(preferences, "compiler", args.compiler, "msvc")
    args.target = resolve_preference(preferences, "target", args.target, project_name())

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
