#!/usr/bin/env python3
"""Run clang-tidy on C++ sources in advisory mode."""

import subprocess
import sys
from pathlib import Path
from typing import Optional


def find_clang_tidy() -> Optional[str]:
    """Find clang-tidy executable."""
    candidates = [
        "clang-tidy",
        "clang-tidy.exe",
        "clang-tidy-14",
        "clang-tidy-13",
        "clang-tidy-12",
    ]

    for candidate in candidates:
        try:
            result = subprocess.run(
                [candidate, "--version"],
                capture_output=True,
                timeout=5,
                text=True
            )
            if result.returncode == 0:
                return candidate
        except Exception:
            continue

    return None


def main() -> int:
    """Run clang-tidy on src/ .cpp files in advisory mode."""
    src_root = Path(__file__).parent.parent.parent / "src"
    report_path = Path(__file__).parent.parent.parent / "build" / "clang-tidy-report.txt"

    # Find clang-tidy
    clang_tidy = find_clang_tidy()
    if not clang_tidy:
        print("clang-tidy not found; skipping (advisory mode)")
        return 0

    print(f"Running clang-tidy ({clang_tidy}) on src/...")

    # Find all .cpp files, exclude Vendor/
    cpp_files = [
        str(f) for f in src_root.rglob("*.cpp")
        if "Vendor" not in f.parts
    ]

    if not cpp_files:
        print("No .cpp files found in src/")
        return 0

    # Run clang-tidy
    try:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        with open(report_path, "w") as report_file:
            result = subprocess.run(
                [clang_tidy, "-p", str(Path(__file__).parent.parent.parent / "build" / "generated" / "vs2022")] + cpp_files,
                stdout=report_file,
                stderr=subprocess.STDOUT,
                timeout=300,
                text=True
            )
    except subprocess.TimeoutExpired:
        print("clang-tidy timed out (advisory mode - continuing)")
        return 0
    except Exception as e:
        print(f"Error running clang-tidy: {e} (advisory mode - continuing)")
        return 0

    # Parse and summarize report
    try:
        with open(report_path) as f:
            content = f.read()

        warning_count = content.count("warning:")
        error_count = content.count("error:")

        print(f"clang-tidy: {warning_count} warnings, {error_count} errors (advisory)")
        print(f"Report: {report_path}")
    except Exception as e:
        print(f"Could not parse report: {e}")

    # Advisory mode: always return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
