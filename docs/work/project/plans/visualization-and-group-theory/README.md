# Visualization primitives, orbitals, and point-group analysis

_Locked via grill — by Codex + user, 2026-09-11. Split into per-workstream files 2026-09-11._

## Goal

Build a reusable, publication-oriented visualization layer for Defect Studio and connect it to a
point-group workflow for defects. The first complete acceptance case is NV⁻ in diamond: define a
basis from atoms, bonds, procedural orbitals, and/or electronic-structure orbitals; detect or choose
C₃ᵥ; construct and reduce representations with the existing `groupy` package; generate
symmetry-adapted orbitals and many-electron terms; and present the results as editable 2D/3D scene
objects, readable tables, and rendered LaTeX/Markdown. The work prioritizes clear, attractive,
repeatable visualization while keeping the provenance and limits of computed results explicit.

## Workstreams

Each workstream is one branch (`task/NN-short-name`), merged to `main` on its own after a green
build. No long-lived epic branch — the app must work after every merge.

| # | File | Branch | Status |
|---|------|--------|--------|
| 1 | [groupy-bridge.md](01-groupy-bridge.md) | `task/19-groupy-bridge-spike` | in progress |
| 2 | [scene-object-model.md](02-scene-object-model.md) | `task/20-scene-object-model` | not started |
| 3 | [planes-and-labels.md](03-planes-and-labels.md) | `task/21-planes-and-labels` | not started |
| 4 | [paths-and-arrows.md](04-paths-and-arrows.md) | — | not started |
| 5 | [bonds-and-orbitals.md](05-bonds-and-orbitals.md) | — | not started |
| 6 | [electronic-structure-integration.md](06-electronic-structure-integration.md) | — | not started |
| 7 | [basis-objects.md](07-basis-objects.md) | — | not started |
| 8 | [point-group-analysis.md](08-point-group-analysis.md) | — | not started |
| 9 | [nv-acceptance.md](09-nv-acceptance.md) | — | not started |
| 10 | [result-presentation.md](10-result-presentation.md) | — | not started |
| 11 | [deferred.md](11-deferred.md) | — | deferred by decision |

Ordering note: the locked plan put the `groupy` bridge last. It runs **first** as a thin vertical
spike instead, because the bridge payload contract is the largest unknown in the plan (see Risks)
and everything downstream is designed against it. If the spike shows the contract cannot carry
symbolic results, the visual workstreams change shape — better to learn that in week one.

## Key decisions & tradeoffs

- **Visual foundation before scientific workflow:** paths, arrows, planes, bonds, orbitals, styles,
  labels, and the Properties Panel come first so group-theory results have a usable presentation
  layer. (Exception: the bridge spike, see ordering note above.)
- **One object, two presentations:** scene and diagram views are representations of the same object,
  not duplicated objects.
- **Standalone plus semantic:** every visual object can be independently edited; links to atoms,
  structures, operations, and calculations are optional.
- **Style presets plus overrides:** presets enforce consistency while local overrides support
  publication figures and special cases such as NV⁻.
- **Piecewise Bézier paths:** quadratic/cubic Bézier segments give deterministic, editable
  spline-like curves.
- **Renderer-local first, persistence second:** use the existing SceneArrow architecture and
  postpone project serialization until the shared annotation model is stable.
- **`groupy` is authoritative for group theory:** Defect Studio integrates it through jobs/bridge
  and presents its exact/numeric results; it does not duplicate its character tables, reduction,
  projection, or multiplet engine.
- **Automatic labels with explicit overrides:** irreps are computed, not guessed; manual overrides
  are possible only as visibly marked assumptions.
- **Illustrative versus computed:** visual orbital presets may be stylized, but computed
  WAVECAR/groupy provenance and limitations remain visible.
- **NV⁻ first:** the NV tutorial provides the concrete end-to-end acceptance case and a natural
  route to general defects later.
- **No FERWE/FERDO yet:** file generation follows analysis stabilization, not the first group-theory
  UI.

## Risks / open questions

- The exact bridge payloads and serialization contract between Defect Studio and `groupy` still need
  to be designed and tested against symbolic and numeric paths.
- Geometry-derived representations require robust operation-to-component mapping, including orbital
  orientation and phase conventions.
- Mixing procedural orbitals and WAVECAR grids is visually useful but may not be a mathematically
  normalized basis; the UI must state this clearly.
- Persistent object ownership and migration remain a separate stage after renderer-local rework.
- Rich LaTeX/Markdown rendering must reuse available dependencies or add the smallest justified
  renderer; no full notebook engine is intended.
- `LRthesis.pdf` was located at `C:\Users\fzabi\Desktop\master-theisis\03_PDF_Library\LRthesis.pdf`,
  but its text extraction is currently blocked by the local MiKTeX PDF utility initialization.
  Appendix/state conventions remain to be cross-checked before implementing FERWE/FERDO or any
  thesis-specific state export.
- The existing `PLAN.md` concerns another locked workstream and must remain untouched.

## Out of scope

- Full space-group and k-point analysis.
- Immediate FERWE/FERDO generation.
- Immediate diffusion/supercell path file generation.
- Automatic chemical bond inference.
- A second C++ group-theory engine or duplicate character-table database.
- Full Markdown/notebook implementation.
- Full publication-layout editor, SVG/PDF export pipeline, or multi-panel figure composer.
- Claims that stylized orbital geometry is a physically normalized wavefunction.
