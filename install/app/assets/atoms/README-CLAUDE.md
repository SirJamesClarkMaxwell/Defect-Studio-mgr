# Atoms Assets (Single-File Layout)

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
