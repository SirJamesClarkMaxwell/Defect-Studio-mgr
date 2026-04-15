# Build and Validation

This section collects the scripts and pages used to validate the project locally.

Start here if you want to verify the repository on your machine.

## Subpages

- [Build Matrix](build-matrix.md)
- [Test Matrix (Release/Dist)](test-matrix-release-dist.md)
- [Test Strategy](test-strategy.md)

## Documentation Diagram Generation

Event-system diagrams are authored as PlantUML sources in `docs/mdbook/diagrams/*.puml`.

During docs build, SVG files are generated into `docs/mdbook/diagrams/generated/` before `mdbook build`.

Canonical command:

```powershell
scripts\Windows\BuildDocs.bat
```

The `--serve` mode is available for local preview after build:

```powershell
scripts\Windows\BuildDocs.bat --serve
```
