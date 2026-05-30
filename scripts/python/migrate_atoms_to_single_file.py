#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import shutil
import textwrap
from pathlib import Path
from typing import Any

import yaml


class LiteralBlockString(str):
    pass


def _literal_block_representer(dumper: yaml.Dumper, data: LiteralBlockString) -> yaml.ScalarNode:
    return dumper.represent_scalar("tag:yaml.org,2002:str", data, style="|")


yaml.add_representer(LiteralBlockString, _literal_block_representer, Dumper=yaml.SafeDumper)


def read_yaml_file(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        loaded = yaml.safe_load(stream)
    if not isinstance(loaded, dict):
        raise ValueError(f"Expected mapping in {path}")
    return loaded


def wrap_base64(data: bytes) -> LiteralBlockString:
    encoded = base64.b64encode(data).decode("ascii")
    wrapped = "\n".join(textwrap.wrap(encoded, 120))
    return LiteralBlockString(wrapped)


def radial_sort_key(radial_entry: dict[str, Any]) -> tuple[int, str]:
    n_value = int(radial_entry.get("n", 0))
    orbital = str(radial_entry.get("orbital", ""))
    return n_value, orbital


def definition_sort_key(definition_entry: dict[str, Any]) -> str:
    return str(definition_entry.get("id", ""))


def create_element_bundle(
    element: str,
    radial_entries: list[dict[str, Any]],
    definition_entries: list[dict[str, Any]],
) -> dict[str, Any]:
    return {
        "schema_version": 2,
        "kind": "element_orbital_bundle",
        "element": element,
        "radial_orbitals": sorted(radial_entries, key=radial_sort_key),
        "orbital_definitions": sorted(definition_entries, key=definition_sort_key),
    }


def write_yaml_file(path: Path, content: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    dumped = yaml.safe_dump(content, sort_keys=False, allow_unicode=False, width=140)
    path.write_text(dumped, encoding="utf-8", newline="\n")


def build_element_payloads(atoms_dir: Path) -> dict[str, dict[str, list[dict[str, Any]]]]:
    radial_dir = atoms_dir / "radial"
    definitions_dir = atoms_dir / "definitions"

    payloads: dict[str, dict[str, list[dict[str, Any]]]] = {}

    for radial_yaml in sorted(radial_dir.glob("*.yaml")):
        metadata = read_yaml_file(radial_yaml)
        element = str(metadata.get("element", "")).strip()
        if not element:
            raise ValueError(f"Missing 'element' field in {radial_yaml}")

        bin_path = radial_dir / str(metadata.get("data_file", f"{radial_yaml.stem}.bin"))
        if not bin_path.exists():
            bin_path = radial_dir / f"{radial_yaml.stem}.bin"
        if not bin_path.exists():
            raise FileNotFoundError(f"Missing radial binary for {radial_yaml.name}")

        raw_samples = bin_path.read_bytes()
        radial_entry: dict[str, Any] = {}
        for field_name in ("orbital", "n", "l", "zeff", "method", "grid", "normalization", "dtype", "endian", "notes"):
            if field_name in metadata:
                radial_entry[field_name] = metadata[field_name]
        radial_entry["sample_count"] = int(len(raw_samples) / 4)
        radial_entry["data_encoding"] = "base64"
        radial_entry["data_format"] = "float32_little_endian"
        radial_entry["samples_base64"] = wrap_base64(raw_samples)

        payload = payloads.setdefault(element, {"radial_orbitals": [], "orbital_definitions": []})
        payload["radial_orbitals"].append(radial_entry)

    for definition_yaml in sorted(definitions_dir.glob("*.yaml")):
        definition = read_yaml_file(definition_yaml)
        element = str(definition.get("element", "")).strip()
        if not element:
            raise ValueError(f"Missing 'element' field in {definition_yaml}")
        payload = payloads.setdefault(element, {"radial_orbitals": [], "orbital_definitions": []})
        payload["orbital_definitions"].append(definition)

    return payloads


def write_manifest(atoms_dir: Path, bundles: dict[str, dict[str, list[dict[str, Any]]]]) -> None:
    manifest_entries: list[dict[str, Any]] = []
    for element in sorted(bundles.keys()):
        payload = bundles[element]
        manifest_entries.append(
            {
                "element": element,
                "file": f"elements/{element}.atom.yaml",
                "radial_orbitals": len(payload["radial_orbitals"]),
                "orbital_definitions": len(payload["orbital_definitions"]),
            }
        )

    manifest = {
        "schema_version": 1,
        "kind": "atoms_manifest",
        "element_count": len(manifest_entries),
        "elements": manifest_entries,
    }
    write_yaml_file(atoms_dir / "manifest.yaml", manifest)


def write_schema(atoms_dir: Path) -> None:
    schema = {
        "schema_version": 1,
        "id": "element_orbital_bundle_schema",
        "fields": {
            "schema_version": {"type": "int", "required": True},
            "kind": {"type": "str", "required": True, "enum": ["element_orbital_bundle"]},
            "element": {"type": "str", "required": True},
            "radial_orbitals": {
                "type": "list",
                "required": True,
                "item_fields": {
                    "orbital": {"type": "str", "required": True},
                    "n": {"type": "int", "required": True},
                    "l": {"type": "int", "required": True},
                    "zeff": {"type": "float", "required": True},
                    "method": {"type": "str", "required": True},
                    "grid": {"type": "mapping", "required": True},
                    "normalization": {"type": "str", "required": True},
                    "dtype": {"type": "str", "required": True},
                    "endian": {"type": "str", "required": False},
                    "sample_count": {"type": "int", "required": True},
                    "data_encoding": {"type": "str", "required": True, "enum": ["base64"]},
                    "data_format": {"type": "str", "required": True, "enum": ["float32_little_endian"]},
                    "samples_base64": {"type": "str", "required": True},
                },
            },
            "orbital_definitions": {
                "type": "list",
                "required": True,
                "description": "Entries copied from previous definitions/*.yaml files.",
            },
        },
    }
    write_yaml_file(atoms_dir / "schema" / "element_orbital_bundle.yaml", schema)


def write_readme(atoms_dir: Path) -> None:
    readme = """# Atoms Assets (Single-File Layout)

This folder is organized so you can hand it directly to Claude without extra path stitching.

## Structure

- `manifest.yaml`: index of all element bundles.
- `schema/element_orbital_bundle.yaml`: schema for element bundle files.
- `elements/<Element>.atom.yaml`: exactly one file per element.

## Element Bundle Content

Each `elements/<Element>.atom.yaml` contains:

- element metadata (`schema_version`, `kind`, `element`)
- `radial_orbitals`: radial data and metadata for that element
- `orbital_definitions`: orbital/hybrid/crystal orbital definitions for that element

Radial samples are embedded as `samples_base64`:

- `data_encoding: base64`
- `data_format: float32_little_endian`
- `sample_count`: number of float32 samples

## Python decode example

```python
import base64
import numpy as np
import yaml

bundle = yaml.safe_load(open("elements/P.atom.yaml", "r", encoding="utf-8"))
entry = bundle["radial_orbitals"][0]
raw = base64.b64decode(entry["samples_base64"])
values = np.frombuffer(raw, dtype="<f4")  # little-endian float32
```
"""
    (atoms_dir / "README-CLAUDE.md").write_text(readme, encoding="utf-8", newline="\n")


def remove_legacy_directories(atoms_dir: Path) -> None:
    for name in ("definitions", "radial"):
        path = atoms_dir / name
        if path.exists():
            shutil.rmtree(path)
    old_schema = atoms_dir / "schema"
    if old_schema.exists():
        for legacy_schema in ("orbital_definition.yaml", "radial_metadata.yaml"):
            legacy_path = old_schema / legacy_schema
            if legacy_path.exists():
                legacy_path.unlink()


def migrate(atoms_dir: Path) -> None:
    bundles = build_element_payloads(atoms_dir)
    elements_dir = atoms_dir / "elements"
    if elements_dir.exists():
        shutil.rmtree(elements_dir)
    elements_dir.mkdir(parents=True, exist_ok=True)

    for element in sorted(bundles.keys()):
        payload = bundles[element]
        bundle = create_element_bundle(element, payload["radial_orbitals"], payload["orbital_definitions"])
        write_yaml_file(elements_dir / f"{element}.atom.yaml", bundle)

    write_manifest(atoms_dir, bundles)
    write_schema(atoms_dir)
    write_readme(atoms_dir)
    remove_legacy_directories(atoms_dir)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Migrate atom assets to one-file-per-element format.")
    parser.add_argument(
        "--atoms-dir",
        type=Path,
        default=Path("install/app/assets/atoms"),
        help="Path to the atoms asset directory.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    migrate(args.atoms_dir)
    print(f"Migrated atom assets under: {args.atoms_dir}")


if __name__ == "__main__":
    main()
