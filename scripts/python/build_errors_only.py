from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from scripts.python.build import build_job_count
from scripts.python.common.paths import project_name, repo_root, solution_path
from scripts.python.common.tooling import detect_tool

# ponytail: MSBuild's own diagnostic lines always contain "error" (e.g. "1 Error(s)"
# in the summary) even on a clean build - filter on "): error" / ": error " which is
# how MSBuild actually prefixes a real error line, not just the word anywhere.
_ERROR_PATTERN = re.compile(r"\):\s*error\b|^\s*error\b", re.IGNORECASE)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build via MSBuild, write only error lines to build/last_build_errors.txt."
    )
    parser.add_argument("--config", choices=["Debug", "Release", "Dist"], default="Debug")
    parser.add_argument("--target", default=None)
    return parser


def main() -> int:
    args = make_parser().parse_args()

    error_log = repo_root() / "build" / "last_build_errors.txt"
    error_log.parent.mkdir(parents=True, exist_ok=True)
    error_log.write_text("", encoding="utf-8")

    msbuild = detect_tool("msbuild")
    if not msbuild:
        print("[error] MSBuild not found.")
        return 1

    solution = solution_path("vs2022")
    if not solution.exists():
        print(f"[error] Solution not found: {solution}")
        return 1

    target = args.target or project_name()
    command = [
        msbuild,
        str(solution),
        f"/t:{target}",
        f"/p:Configuration={args.config}",
        "/p:Platform=x64",
        f"/maxcpucount:{build_job_count()}",
        "/nologo",
        "/v:minimal",
    ]
    print(f"[cmd] {' '.join(command)}")
    result = subprocess.run(command, cwd=str(repo_root()), capture_output=True, text=True)

    error_lines = [line for line in result.stdout.splitlines() if _ERROR_PATTERN.search(line)]
    error_lines += [line for line in result.stderr.splitlines() if line.strip()]
    error_log.write_text(
        "\n".join(error_lines) + ("\n" if error_lines else ""), encoding="utf-8"
    )

    if result.returncode == 0 and not error_lines:
        print(f"[build] OK ({args.config}, {target})")
    else:
        print(f"[build] FAILED, {len(error_lines)} error line(s) -> {error_log}")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
