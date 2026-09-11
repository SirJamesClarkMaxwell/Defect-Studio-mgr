# 5. Bonds and procedural orbitals

Source: locked plan point 5

## Scope

- Make bonds standalone or optionally anchored to two atoms.
- Store an abstract bond type/order while initially rendering single, double, triple, and dashed
  styles; do not infer chemistry automatically from distances.
- Make orbitals first-class objects with optional atom/atom-group links, type, orientation, scale,
  phase, color, and style.
- Provide attractive procedural presets for `s`, `p`, `d`, `sp`, `sp²`, `sp³`, `σ`, `σ*`, `π`, `π*`,
  `δ`, and `δ*`.
- Allow global shape controls and per-lobe overrides, including elongation and width of ellipsoidal
  lobes for cases such as NV⁻ in diamond.

## Reuse in this repo

`BondComponent` in `SceneComponents.hpp` already carries atom back-references and a colour gradient.
`src/Renderer/Scene/IsosurfaceMesher.{hpp,cpp}` is the existing isosurface path for orbital-shaped
geometry.
