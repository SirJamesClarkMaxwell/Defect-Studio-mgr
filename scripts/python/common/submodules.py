from __future__ import annotations

from scripts.python.common.exec import run_command
from scripts.python.common.paths import repo_root


def ensure_git_submodules(*, dry_run: bool, verbose: bool) -> int:
    code = run_command(
        ["git", "submodule", "update", "--init", "--recursive", "--force"],
        cwd=repo_root(),
        dry_run=dry_run,
        verbose=verbose,
    )
    if code == 0:
        return 0

    print("[warn] Pinned submodule update failed; retrying configured remote branches.")
    return run_command(
        ["git", "submodule", "update", "--remote", "--init", "--recursive", "--force"],
        cwd=repo_root(),
        dry_run=dry_run,
        verbose=verbose,
    )
