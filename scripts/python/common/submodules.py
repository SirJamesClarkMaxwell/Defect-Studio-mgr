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
    if code != 0:
        print(
            "[error] Submodule update failed. Usually a pinned commit is missing from its "
            "remote - push it to the fork, then re-run."
        )
        print(
            "[error] Do NOT retry with 'git submodule update --remote': it re-pins every "
            "submodule (including nested ones such as freetype) to its branch tip, which "
            "breaks the build in ways that look unrelated."
        )
    return code
