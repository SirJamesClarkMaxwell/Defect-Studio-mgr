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

Each workstream is one branch (`task/NN-short-name`), merged to `dev` on its own after a green
build (`main` later). No long-lived epic branch — the app must work after every merge.

Branch numbering and order now follow the 2026-09-13 plan
[scene tools + group-theory panel](../2026-09-13-scene-tools-and-group-theory.md), which supersedes
the branch names first assigned here.

| # | File | Branch | Status |
|---|------|--------|--------|
| 1 | [groupy-bridge.md](01-groupy-bridge.md) | `task/19-groupy-bridge-spike` | merged to `dev` |
| 2 | [scene-object-model.md](02-scene-object-model.md) | `task/20-scene-object-model` | merged to `dev` |
| 3 | [planes-and-labels.md](03-planes-and-labels.md) | `task/31-planes` (labels landed with task/20) | not started |
| 4 | [paths-and-arrows.md](04-paths-and-arrows.md) | `task/30-bezier-curves` | not started |
| 5 | [bonds-and-orbitals.md](05-bonds-and-orbitals.md) | `task/26-bonds-and-orbitals` | not started |
| 6 | [electronic-structure-integration.md](06-electronic-structure-integration.md) | — | out of scope for now (WAVECAR bases) |
| 7 | [basis-objects.md](07-basis-objects.md) | `task/27-basis-objects` | not started |
| 8 | [point-group-analysis.md](08-point-group-analysis.md) | `task/23-group-theory-panel` (atom basis), `task/28-group-theory-panel-v2` (orbital basis) | task/23 backend in progress |
| 9 | [nv-acceptance.md](09-nv-acceptance.md) | `task/28-group-theory-panel-v2` | not started (multiplet table checked in task/23) |
| 10 | [result-presentation.md](10-result-presentation.md) | `task/23-group-theory-panel` (tables, copy as Markdown/LaTeX) | not started; full rendering out of scope |
| 11 | [deferred.md](11-deferred.md) | — | deferred by decision |

Numbering note: `task/22-process-tree-cleanup` is **not** part of this plan — it is the Windows
Job Object fix for zombie Python grandchildren (merged to `dev`).

Serialization note: workstreams 2, 3, 4 and 5 all land on `SceneComponents.hpp` and
`ObjectPropertiesPanel.cpp`. They are **not** parallelizable — 2 is the foundation and the rest
queue behind it. Workstream 1 is the only one independent of that file set, which is why it and
2 are the only two in flight.

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
