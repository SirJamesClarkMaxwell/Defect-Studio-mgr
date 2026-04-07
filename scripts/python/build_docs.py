from __future__ import annotations

import argparse
import sys

from scripts.python.common.cli import print_header, print_step
from scripts.python.common.exec import run_command
from scripts.python.common.paths import repo_root
from scripts.python.common.tooling import detect_tool


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build project documentation.")
    parser.add_argument(
        "--serve",
        action="store_true",
        help="Serve docs locally after build.",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=3000,
        help="Port used when serving docs locally.",
    )
    parser.add_argument(
        "--open",
        action="store_true",
        help="Open the built docs in browser after building.",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def run(args: argparse.Namespace) -> int:
    mdbook = detect_tool("mdbook")
    if not mdbook:
        print("[error] mdbook not found.")
        return 1

    print_header("Build Documentation")
    docs_dir = repo_root() / "docs"

    if not docs_dir.exists():
        print(f"[error] docs directory not found: {docs_dir}")
        return 1

    print_step("Building documentation with mdbook")
    code = run_command(
        [mdbook, "build"],
        cwd=docs_dir,
        dry_run=args.dry_run,
        verbose=args.verbose,
    )
    if code != 0:
        return code

    preview_url = f"http://localhost:{args.port}"
    print_step(f"Preview URL: {preview_url}")
    print("       Run with --serve to host docs on localhost.")

    if args.serve:
        print_step(f"Serving documentation on {preview_url}")
        return run_command(
            [mdbook, "serve", "--hostname", "127.0.0.1", "--port", str(args.port)],
            cwd=docs_dir,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )

    if args.open:
        print_step("Opening documentation in browser")
        import platform

        if platform.system() == "Windows":
            html_index = docs_dir / "book" / "index.html"
            run_command(
                ["cmd", "/c", "start", "", str(html_index)],
                cwd=repo_root(),
                dry_run=args.dry_run,
                verbose=args.verbose,
            )
        else:
            html_index = docs_dir / "book" / "index.html"
            run_command(
                ["xdg-open", str(html_index)],
                cwd=repo_root(),
                dry_run=args.dry_run,
                verbose=args.verbose,
            )

    return 0


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
