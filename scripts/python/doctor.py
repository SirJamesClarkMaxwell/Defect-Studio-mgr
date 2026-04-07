from __future__ import annotations

import argparse
import platform
import sys

from scripts.python.common.config import load_local_config
from scripts.python.common.tooling import detect_many


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Print local environment diagnostics.")
    parser.add_argument("--verbose", action="store_true")
    return parser


def run(args: argparse.Namespace) -> int:
    print("== Doctor ==")
    print(f"Platform: {platform.system()} {platform.release()}")

    tools = detect_many(["git", "python", "uv", "premake5", "mdbook", "tracy", "msbuild", "make", "gcc", "clang", "code"])
    for name, path in tools.items():
        status = path if path else "MISSING"
        print(f"- {name:8} {status}")

    local_cfg = load_local_config()
    if local_cfg:
        print("Local toolchain config: present")
        if args.verbose:
            print(local_cfg)
    else:
        print("Local toolchain config: missing")

    return 0


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
