from __future__ import annotations

import os
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def project_name() -> str:
    return "DefectStudio"


def project_executable_name() -> str:
    return f"{project_name()}.exe" if os.name == "nt" else project_name()


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


def plantuml_dir() -> Path:
    return vendor_binaries_dir() / "PlantUML"


def tracy_dir() -> Path:
    if os.name == "nt":
        return vendor_binaries_dir() / "Tracy" / "Windows"
    return vendor_binaries_dir() / "Tracy" / "Linux"


def tracy_profiler_path() -> Path:
    return tracy_dir() / ("tracy-profiler.exe" if os.name == "nt" else "tracy-profiler")


def local_config_path() -> Path:
    return repo_root() / ".local" / "toolchain.json"


def logs_dir() -> Path:
    return repo_root() / "logs"


def build_dir() -> Path:
    return repo_root() / "build"


def generated_dir(generator: str = "vs2022") -> Path:
    return build_dir() / "generated" / generator


def solution_path(generator: str = "vs2022") -> Path:
    return generated_dir(generator) / f"{project_name()}.sln"


def app_executable_path(config: str, arch: str = "x86_64") -> Path:
    system = "windows" if os.name == "nt" else "linux"
    output_key = f"{config}-{system}-{arch}"
    return build_dir() / "bin" / output_key / project_name() / project_executable_name()


def test_executable_path(config: str, arch: str = "x86_64") -> Path:
    system = "windows" if os.name == "nt" else "linux"
    output_key = f"{config}-{system}-{arch}"
    name = "DefectStudioTests.exe" if os.name == "nt" else "DefectStudioTests"
    return build_dir() / "bin" / output_key / "DefectStudioTests" / name
