# 8. Point-group analysis UI

Source: locked plan point 8 (UI half — the bridge itself is workstream 1)

## Scope

- Reuse existing spglib-based symmetry detection and its separate configurable detection tolerance
  and symmetrization tolerance.
- Support automatic point-group detection plus deliberate manual selection/override, with provenance
  and warnings.
- Display character tables, classes, irreducible representations, reducible representations,
  reductions, projection results, state labels, degeneracies, and multiplet information.
- Automatically assign irreps from the computed representation. Manual labels are allowed only as
  explicit assumptions and must be visibly marked.
- Generate linked, regenerable visual overlays for axes/rotations, planes/reflections, inversion
  centers, and related operations; allow per-operation visibility and style, detachment, and manual
  editing.
- Mark analysis results stale after structure or basis changes and offer recomputation or snapshot
  retention.
