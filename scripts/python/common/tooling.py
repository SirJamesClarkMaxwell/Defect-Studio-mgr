from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

from .paths import mdbook_dir, premake_dir, tools_dir, tracy_profiler_path


TOOL_HINTS: dict[str, list[str]] = {
    "git": ["git"],
    "python": ["python", "py"],
    "uv": ["uv"],
    "premake5": ["premake5"],
    "mdbook": ["mdbook"],
    "msbuild": ["MSBuild"],
    "code": ["code"],
    "tracy": ["tracy-profiler", "Tracy"],
    "make": ["make", "gmake", "mingw32-make"],
    "gcc": ["gcc"],
    "clang": ["clang"],
}


def _platform_folder() -> str:
    return "Windows" if os.name == "nt" else "Linux"


def _repo_local_tool_path(name: str) -> str | None:
    base = None
    if name == "premake5":
        base = (
            premake_dir()
            / _platform_folder()
            / ("premake5.exe" if os.name == "nt" else "premake5")
        )
    elif name == "mdbook":
        base = (
            mdbook_dir()
            / _platform_folder()
            / "bin"
            / ("mdbook.exe" if os.name == "nt" else "mdbook")
        )
    elif name in {"tracy", "tracy-profiler"}:
        base = tracy_profiler_path()
    else:
        base = tools_dir() / name / name

    if base.exists():
        return str(base)

    if base.with_suffix(".exe").exists():
        return str(base.with_suffix(".exe"))

    return None


def require_vendored_tool(name: str) -> str:
    path = _repo_local_tool_path(name)
    if path:
        return path
    raise FileNotFoundError(f"Vendored tool not found: {name}")


def detect_tool(name: str) -> str | None:
    if name in {"premake5", "mdbook", "tracy", "tracy-profiler"}:
        try:
            return require_vendored_tool(name)
        except FileNotFoundError:
            return None

    if name == "msbuild":
        msbuild = detect_msvc_msbuild_path()
        if msbuild:
            return msbuild

    candidates = TOOL_HINTS.get(name, [name])
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return found

    local_candidate = _repo_local_tool_path(name)
    if local_candidate:
        return local_candidate

    return None


def detect_msvc_installation_path() -> str | None:
    if os.name != "nt":
        return None

    vswhere = shutil.which("vswhere")
    if not vswhere:
        candidate_locations = [
            Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
            / "Microsoft Visual Studio"
            / "Installer"
            / "vswhere.exe",
            Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
            / "Microsoft Visual Studio"
            / "Installer"
            / "vswhere.exe",
        ]
        for candidate in candidate_locations:
            if candidate.exists():
                vswhere = str(candidate)
                break
    if not vswhere:
        return None

    result = subprocess.run(
        [
            vswhere,
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.Component.MSBuild",
            "-property",
            "installationPath",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    installation_path = result.stdout.strip()
    return installation_path or None


def detect_msvc_msbuild_path() -> str | None:
    installation_path = detect_msvc_installation_path()
    if not installation_path:
        return None

    installation_root = Path(installation_path)
    msbuild_roots = sorted(installation_root.glob("MSBuild/**/Bin"), reverse=True)
    for msbuild_root in msbuild_roots:
        candidate = msbuild_root / ("MSBuild.exe" if os.name == "nt" else "MSBuild")
        if candidate.exists():
            return str(candidate)
    return None


def detect_msvc_compiler_path() -> str | None:
    installation_path = detect_msvc_installation_path()
    if not installation_path:
        return None

    installation_root = Path(installation_path)
    compiler_roots = sorted(
        (installation_root / "VC" / "Tools" / "MSVC").glob("*"), reverse=True
    )
    for compiler_root in compiler_roots:
        candidate = (
            compiler_root
            / "bin"
            / "Hostx64"
            / "x64"
            / ("cl.exe" if os.name == "nt" else "cl")
        )
        if candidate.exists():
            return str(candidate)
    return None


def detect_many(names: list[str]) -> dict[str, str | None]:
    return {name: detect_tool(name) for name in names}


def is_windows() -> bool:
    return Path("C:/").exists()
