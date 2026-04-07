from __future__ import annotations

import subprocess
from pathlib import Path


def run_command(command: list[str], *, cwd: Path, dry_run: bool, verbose: bool) -> int:
    cmd_display = " ".join(command)
    print(f"[cmd] {cmd_display}")
    if dry_run:
        return 0
    result = subprocess.run(command, cwd=str(cwd), check=False)
    if verbose:
        print(f"[exit] {result.returncode}")
    return result.returncode
