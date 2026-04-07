from __future__ import annotations

import os
import subprocess
from pathlib import Path


def run_command(
    command: list[str],
    *,
    cwd: Path,
    dry_run: bool,
    verbose: bool,
    env: dict[str, str] | None = None,
) -> int:
    cmd_display = " ".join(command)
    print(f"[cmd] {cmd_display}")
    if dry_run:
        return 0
    full_env = os.environ.copy()
    if env:
        full_env.update(env)
    result = subprocess.run(command, cwd=str(cwd), check=False, env=full_env)
    if verbose:
        print(f"[exit] {result.returncode}")
    return result.returncode
