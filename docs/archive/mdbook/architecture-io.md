# IO Architecture

The IO layer owns parsing, validation, and export boundaries.

## Scope

IO responsibilities:

- Import files into runtime DTO and domain entities.
- Export canonical project and scientific outputs.
- Keep parser behavior deterministic and testable.

## Planned Import and Export Areas

From TODO:

- POSCAR and CONTCAR parser and precise export.
- CIF and XYZ import paths.
- OUTCAR and WAVECAR metadata extraction.
- Future bundle import and export envelope for scientific records.

## Error Contract

IO failures should surface as structured errors:

- technical details for logs,
- concise user-facing messages for UI policy.
