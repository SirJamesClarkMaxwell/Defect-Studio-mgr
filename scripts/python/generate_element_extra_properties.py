"""Enrich install/app/data/elements/element_properties.yaml with electronegativity and
ground-state electron configuration, sourced from pymatgen (already a project dependency via
PymatgenBridge) rather than hand-typed to avoid transcription errors in scientific data.

Preserves every existing field/entry in the file and only appends the two new keys per element.
Run from the repo root: python -m scripts.python.generate_element_extra_properties
Writes the result back to the same file in place.
"""

import re
import sys
from pathlib import Path

from pymatgen.core.periodic_table import Element

REPO_ROOT = Path(__file__).resolve().parents[2]
TARGET = REPO_ROOT / "install" / "app" / "data" / "elements" / "element_properties.yaml"

LINE_RE = re.compile(r"^\s*([A-Za-z]{1,2}):\s*\{ (.+) \}\s*$")


def enrich_line(line: str) -> str:
    match = LINE_RE.match(line)
    if not match:
        return line.rstrip("\n")

    symbol, body = match.group(1), match.group(2)
    # Re-runnable: strip any electronegativity/electron_configuration this script already added on
    # a previous pass, rather than appending duplicate keys.
    body = re.sub(r",\s*electronegativity:[^,}]+", "", body)
    body = re.sub(r",\s*electron_configuration:\s*\"[^\"]*\"", "", body)
    try:
        element = Element(symbol)
    except Exception as exc:
        print(f"WARN: {symbol}: {exc}", file=sys.stderr)
        return line.rstrip("\n")

    try:
        x = element.X
        electronegativity = 0.0 if (x is None or x != x) else round(float(x), 2)
    except Exception:
        electronegativity = 0.0

    try:
        shells = element.full_electronic_structure  # list of (n, subshell, electron_count)
        configuration = " ".join(f"{n}{subshell}{count}" for n, subshell, count in shells)
    except Exception as exc:
        print(f"WARN config {symbol}: {exc}", file=sys.stderr)
        configuration = ""

    new_body = f"{body}, electronegativity: {electronegativity}, electron_configuration: \"{configuration}\""
    return f"  {symbol}: {{ {new_body} }}"


def main() -> None:
    lines = TARGET.read_text(encoding="utf-8").splitlines()
    enriched = [enrich_line(line) for line in lines]
    TARGET.write_text("\n".join(enriched) + "\n", encoding="utf-8")
    print(f"Wrote {TARGET}")


if __name__ == "__main__":
    main()
