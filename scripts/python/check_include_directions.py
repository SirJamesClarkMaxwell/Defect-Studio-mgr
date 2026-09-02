#!/usr/bin/env python3
"""Check include direction dependencies between architecture modules."""

import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Set, Tuple, Optional


@dataclass
class Rule:
    """Single rule: from_module can include to_module_list."""
    from_module: str
    to_modules: Set[str]


@dataclass
class Violation:
    """Include direction violation."""
    file_path: str
    line_number: int
    from_module: str
    to_module: str
    reason: str = ""

    def __str__(self):
        return f"{self.file_path}:{self.line_number}: {self.from_module} -> {self.to_module} (niedozwolone)"


class CheckIncludeDirections:
    """Validates include directions between modules."""

    MODULES = {
        "App", "Core", "Debug", "Demo", "Domain", "Events", "IO",
        "Presentation", "Renderer", "ScientificRuntime", "Storage"
    }

    def __init__(self, src_root: Path, rules: Dict):
        """Initialize checker with rules."""
        self.src_root = Path(src_root)
        self.rules = rules
        self.violations: List[Violation] = []
        self.edges: Set[Tuple[str, str]] = set()  # For --print-matrix

    def check(self) -> List[Violation]:
        """Scan src/ and check all includes."""
        self.violations = []
        self.edges = set()

        for cpp_file in self.src_root.rglob("*.[ch]pp"):
            self._check_file(cpp_file)

        # Report violations
        for v in self.violations:
            print(str(v))

        return self.violations

    def _check_file(self, file_path: Path):
        """Check a single file for include violations."""
        try:
            content = file_path.read_text(encoding="utf-8", errors="replace")
        except Exception:
            return

        # Determine this file's module
        rel_path = file_path.relative_to(self.src_root)
        parts = rel_path.parts
        if not parts:
            return
        source_module = parts[0]
        if source_module not in self.MODULES:
            return

        # Extract includes
        include_pattern = re.compile(r'#include\s+"([^"]+)"')
        for line_num, line in enumerate(content.split('\n'), 1):
            match = include_pattern.search(line)
            if not match:
                continue

            include_path = match.group(1)
            target_module = include_path.split('/')[0]

            # Skip non-module includes
            if target_module not in self.MODULES:
                continue

            # Record edge
            self.edges.add((source_module, target_module))

            # Check if allowed
            if self._is_allowed(source_module, target_module):
                continue

            # Check if exception
            if self._is_exception(source_module, target_module):
                continue

            # Report violation
            self.violations.append(Violation(
                file_path=str(file_path),
                line_number=line_num,
                from_module=source_module,
                to_module=target_module
            ))

    def _is_allowed(self, from_module: str, to_module: str) -> bool:
        """Check if this edge is in allowed list."""
        allowed_dict = self.rules.get("allowed", {})
        if from_module not in allowed_dict:
            return False
        return to_module in allowed_dict[from_module]

    def _is_exception(self, from_module: str, to_module: str) -> bool:
        """Check if this edge is a known exception."""
        for exc in self.rules.get("known_exceptions", []):
            if exc["from"] == from_module and exc["to"] == to_module:
                return True
        return False

    def print_matrix(self):
        """Print detected edges as JSON matrix (for baseline generation)."""
        matrix = {}
        for from_mod in self.MODULES:
            to_mods = sorted([to_mod for (f, to_mod) in self.edges if f == from_mod])
            if to_mods:
                matrix[from_mod] = to_mods

        print(json.dumps({"allowed": matrix}, indent=2))


def load_rules(rules_path: Path) -> Dict:
    """Load rules from JSON file."""
    try:
        return json.loads(rules_path.read_text())
    except Exception as e:
        print(f"Error loading rules from {rules_path}: {e}", file=sys.stderr)
        sys.exit(1)


def main():
    """Main entry point."""
    import argparse

    parser = argparse.ArgumentParser(description="Check include direction dependencies")
    parser.add_argument(
        "--src",
        type=Path,
        default=Path(__file__).parent.parent.parent / "src",
        help="Path to src/ directory"
    )
    parser.add_argument(
        "--rules",
        type=Path,
        default=Path(__file__).parent / "include_rules.json",
        help="Path to include rules JSON"
    )
    parser.add_argument(
        "--print-matrix",
        action="store_true",
        help="Print detected edges as JSON matrix and exit"
    )
    args = parser.parse_args()

    # Load rules (not needed for --print-matrix)
    rules = {}
    if not args.print_matrix:
        if not args.rules.exists():
            print(f"Rules file not found: {args.rules}", file=sys.stderr)
            sys.exit(1)
        rules = load_rules(args.rules)

    # Run checker
    checker = CheckIncludeDirections(args.src, rules)

    if args.print_matrix:
        checker.check()
        checker.print_matrix()
        sys.exit(0)
    else:
        violations = checker.check()
        sys.exit(1 if violations else 0)


if __name__ == "__main__":
    main()
