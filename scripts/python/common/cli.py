from __future__ import annotations

import argparse


def add_common_flags(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--defaults", action="store_true", help="Use default answers in prompts.")
    parser.add_argument("--yes", action="store_true", help="Auto-confirm prompts.")
    parser.add_argument("--ci", action="store_true", help="Use deterministic non-interactive mode.")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose diagnostics.")
    parser.add_argument("--dry-run", action="store_true", help="Print actions without executing them.")


def print_header(title: str) -> None:
    print(f"\n== {title} ==")


def print_step(step: str) -> None:
    print(f"[step] {step}")
