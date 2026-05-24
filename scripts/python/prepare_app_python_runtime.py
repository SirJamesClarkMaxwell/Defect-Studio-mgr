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
from scripts.python.common.paths import repo_root


def detect_platform_tag() -> str:
    if os.name == "nt":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"


def read_venv_home(venv_cfg_path: Path) -> Path | None:
    if not venv_cfg_path.exists():
        return None

    for raw_line in venv_cfg_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if line.startswith("home ="):
            value = line.split("=", 1)[1].strip()
            if value:
                return Path(value)
    return None


def detect_venv_site_packages(venv_dir: Path) -> Path | None:
    windows_site = venv_dir / "Lib" / "site-packages"
    if windows_site.exists():
        return windows_site

    for candidate in sorted((venv_dir / "lib").glob("python*/site-packages")):
        if candidate.exists():
            return candidate
    return None


def copy_tree(src: Path, dst: Path, *, dry_run: bool, verbose: bool) -> None:
    if not src.exists():
        return
    if verbose:
        print(f"[copy-tree] {src} -> {dst}")
    if dry_run:
        return
    shutil.copytree(src, dst, dirs_exist_ok=True)


def copy_file(src: Path, dst: Path, *, dry_run: bool, verbose: bool) -> None:
    if not src.exists():
        return
    if verbose:
        print(f"[copy-file] {src} -> {dst}")
    if dry_run:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Prepare app-local Python runtime under install/app/python."
    )
    parser.add_argument("--source-home", default=None, help="Explicit CPython home path.")
    parser.add_argument("--venv", default=".venv", help="Virtualenv directory for site-packages and home discovery.")
    parser.add_argument("--dest-root", default="install/app/python", help="Destination root for app-local runtime.")
    parser.add_argument("--platform", choices=["windows", "linux", "macos"], default=None)
    parser.add_argument("--clean", action="store_true", help="Remove destination platform directory before copy.")
    parser.add_argument("--skip-site-packages", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def run(args: argparse.Namespace) -> int:
    print_header("Prepare App Python Runtime")
    root = repo_root()
    venv_dir = (root / args.venv).resolve()
    platform_tag = args.platform or detect_platform_tag()
    dest_root = (root / args.dest_root).resolve()
    dest_platform_root = dest_root / platform_tag

    source_home: Path | None
    if args.source_home:
        source_home = Path(args.source_home).resolve()
    else:
        source_home = read_venv_home(venv_dir / "pyvenv.cfg")

    if source_home is None:
        print("[error] Could not resolve CPython home.")
        print("        Pass --source-home or ensure .venv/pyvenv.cfg has a valid 'home = ...' entry.")
        return 1

    if not source_home.exists():
        print(f"[error] CPython home path does not exist: {source_home}")
        return 1

    print_step(f"platform={platform_tag}")
    print_step(f"python_home={source_home}")
    print_step(f"destination={dest_platform_root}")

    if args.clean:
        print_step("clean destination")
        if not args.dry_run and dest_platform_root.exists():
            shutil.rmtree(dest_platform_root)

    runtime_dirs = [
        "DLLs",
        "Lib",
        "libs",
        "include",
        "Include",
        "tcl",
        "Scripts",
        "bin",
    ]
    for directory_name in runtime_dirs:
        src_dir = source_home / directory_name
        dst_dir = dest_platform_root / directory_name
        copy_tree(src_dir, dst_dir, dry_run=args.dry_run, verbose=args.verbose)

    runtime_files = [
        "python.exe",
        "pythonw.exe",
        "python3.dll",
        "python313.dll",
        "python312.dll",
        "python311.dll",
        "libpython3.13.so",
        "libpython3.12.so",
        "libpython3.11.so",
        "libpython3.13.dylib",
        "libpython3.12.dylib",
        "libpython3.11.dylib",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "LICENSE.txt",
    ]
    for file_name in runtime_files:
        copy_file(
            source_home / file_name,
            dest_platform_root / file_name,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )

    if not args.skip_site_packages:
        site_packages = detect_venv_site_packages(venv_dir)
        if site_packages is None:
            print("[warn] venv site-packages not found; skipping scientific package overlay.")
        else:
            target_site_packages = dest_platform_root / "Lib" / "site-packages"
            print_step(f"overlay site-packages: {site_packages} -> {target_site_packages}")
            copy_tree(
                site_packages,
                target_site_packages,
                dry_run=args.dry_run,
                verbose=args.verbose,
            )

    bridge_scripts_source = root / "scripts" / "python" / "examples"
    bridge_scripts_target = dest_platform_root / "scripts" / "python" / "examples"
    print_step("copy bridge scripts")
    copy_tree(
        bridge_scripts_source,
        bridge_scripts_target,
        dry_run=args.dry_run,
        verbose=args.verbose,
    )

    if not args.dry_run:
        manifest = dest_platform_root / "runtime-manifest.txt"
        manifest.write_text(
            "\n".join(
                [
                    f"platform={platform_tag}",
                    f"python_home={source_home}",
                    f"venv={venv_dir}",
                ]
            )
            + "\n",
            encoding="utf-8",
        )

    print("[ok] App-local Python runtime prepared.")
    return 0


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
