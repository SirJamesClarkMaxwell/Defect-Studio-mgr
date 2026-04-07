from __future__ import annotations

from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def scripts_dir() -> Path:
    return repo_root() / "scripts"


def tools_dir() -> Path:
    return repo_root() / "tools"


def vendor_binaries_dir() -> Path:
    return repo_root() / "Vendor" / "Binaries"


def premake_dir() -> Path:
    return vendor_binaries_dir() / "Premake"


def mdbook_dir() -> Path:
    return vendor_binaries_dir() / "MdBook"


def local_config_path() -> Path:
    return repo_root() / ".local" / "toolchain.json"


def logs_dir() -> Path:
    return repo_root() / "logs"


def build_dir() -> Path:
    return repo_root() / "build"
