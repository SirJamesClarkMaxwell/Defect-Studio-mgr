# Data Model Architecture

The Data Model layer defines canonical scientific entities and their identities.

## Current Intent

The model should provide stable, typed structures used by runtime, IO, and scientific adapters.

## Planned Entities

Based on TODO milestones:

- Structure with lattice vectors, atoms, and bonds.
- Atom with fractional canonical position plus properties.
- Bond with pairing, type, and derived metrics.
- Element catalog for rendering and physical metadata.
- Stable UUID identity for runtime and persistence alignment.

## Canonical Policy

Coordinate handling should remain explicit and deterministic:

- canonical fractional storage,
- derived cartesian utilities,
- synchronized editing and serialization rules.
