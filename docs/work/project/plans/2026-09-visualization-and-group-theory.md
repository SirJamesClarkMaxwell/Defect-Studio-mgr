# Plan: Visualization primitives, orbitals, and point-group analysis
_Locked via grill — by Codex + user, 2026-09-11_

## Goal

Build a reusable, publication-oriented visualization layer for Defect Studio and connect it to a point-group workflow for defects. The first complete acceptance case is NV⁻ in diamond: define a basis from atoms, bonds, procedural orbitals, and/or electronic-structure orbitals; detect or choose C₃ᵥ; construct and reduce representations with the existing `groupy` package; generate symmetry-adapted orbitals and many-electron terms; and present the results as editable 2D/3D scene objects, readable tables, and rendered LaTeX/Markdown. The work prioritizes clear, attractive, repeatable visualization while keeping the provenance and limits of computed results explicit.

## Approach

1. **Reuse the existing scene annotation foundation**
   - Extend the existing `SceneArrow`/renderer annotation flow, `ObjectPropertiesPanel`, quick-edit, Scene Outliner, renderer, and local annotation undo.
   - Keep the first rework renderer-local; add project persistence as a separate follow-up stage once the object model is stable.
   - Keep geometry/data separate from appearance and use shared style presets with per-object overrides.

2. **Establish the shared visual-object model**
   - Support standalone objects and optional semantic links to atoms, structures, operations, or electronic-structure sources.
   - Preserve stable IDs, provenance, deterministic serialization, visibility, and stale/regeneration state.
   - Let one object switch between scene-space 3D and diagram/screen-space presentation without duplicating the object.
   - Hide coordinate-system details from the user; expose comfortable manipulators and numeric properties.
   - Reuse the current renderer and PNG/JPG export path.

3. **Improve arrows and paths**
   - Represent a path as ordered control points and piecewise quadratic/cubic Bézier segments.
   - Keep path geometry separate from physical interpretation.
   - Make arrows separate objects that reference a path, so one path can render as a trajectory, curved arrow, vector series, or animation.
   - Expose line pattern, width, opacity, color, border/stroke, head shape/size, and independent start/end heads. Support dashed symmetry axes and one-ended arrows.
   - Use the existing SceneArrow rework plan as the implementation base; do not create a parallel annotation system.

4. **Add planes and scene labels**
   - Support point+normal, three-point, and Miller-index construction modes, converging to one internal representation.
   - Support finite, cell-clipped, and visually infinite modes.
   - Put size, position, rotation, cropping, border, opacity, grid, stroke, and related controls in the existing Properties Panel.
   - Use the same object/style/provenance model for manual planes and analysis-generated symmetry planes.
   - Render LaTeX/Markdown both in panels/tables and in scene labels. Scene labels are camera-facing by default, with optional world/plane orientation.

5. **Add bonds and procedural orbitals**
   - Make bonds standalone or optionally anchored to two atoms.
   - Store an abstract bond type/order while initially rendering single, double, triple, and dashed styles; do not infer chemistry automatically from distances.
   - Make orbitals first-class objects with optional atom/atom-group links, type, orientation, scale, phase, color, and style.
   - Provide attractive procedural presets for `s`, `p`, `d`, `sp`, `sp²`, `sp³`, `σ`, `σ*`, `π`, `π*`, `δ`, and `δ*`.
   - Allow global shape controls and per-lobe overrides, including elongation and width of ellipsoidal lobes for cases such as NV⁻ in diamond.

6. **Integrate existing electronic-structure data**
   - Reuse `ElectronicStructureModel`, `ElectronicStructureSession`, `OrbitalRecord`, `OrbitalGridData`, occupation-diagram state, and existing WAVECAR loading/cache/rendering.
   - Allow a basis component to reference an electronic orbital by calculation/source, spin, and band rather than copying its grid data.
   - Support mixing procedural, manual, bond, WAVECAR, and occupation-diagram components in one visual linear combination.
   - Store coefficients and phase in the basis model; allow visually tuned scaling without changing computed coefficients.
   - Mark combinations whose components lack a validated common basis or normalization.

7. **Create saved basis objects**
   - A basis is a named, saved project object containing ordered explicit components: individual orbitals, bond orbitals, lobes, procedural objects, and electronic-structure states.
   - Provide add-to-basis actions from Properties Panel and a dedicated searchable basis panel with ordering, removal, selection, and scene highlighting.
   - Keep exact coefficients and metadata separate from visual styling.
   - Support both explicit irrep labels and geometry-derived representations.

8. **Integrate point-group analysis through `groupy`**
   - Use the existing Python bridge/job system for analysis; do not implement a second group-theory engine in C++.
   - Reuse existing spglib-based symmetry detection and its separate configurable detection tolerance and symmetrization tolerance.
   - Support automatic point-group detection plus deliberate manual selection/override, with provenance and warnings.
   - Return operation-to-atom/component mappings, displacements, orientations, and phase information.
   - Display character tables, classes, irreducible representations, reducible representations, reductions, projection results, state labels, degeneracies, and multiplet information.
   - Automatically assign irreps from the computed representation. Manual labels are allowed only as explicit assumptions and must be visibly marked.
   - Generate linked, regenerable visual overlays for axes/rotations, planes/reflections, inversion centers, and related operations; allow per-operation visibility and style, detachment, and manual editing.
   - Mark analysis results stale after structure or basis changes and offer recomputation or snapshot retention.

9. **Implement the NV⁻ end-to-end acceptance workflow**
   - Use C₃ᵥ and the four-bond N + 3×C model from `groupy/tutorial/7_NV_center.ipynb`.
   - Build the representation, reduce it to `A1 ⊕ A1 ⊕ E`, and generate projected orbitals `a1'`, `a1`, `ex`, `ey`.
   - Show exact coefficients as rendered LaTeX/Markdown and tables, with copyable Markdown/LaTeX.
   - Run `ActiveSpace` for the four active electrons and show `³A2`, `³E`, `¹E`, and `¹A1` with spin, degeneracy, and provenance.
   - Show determinant/configuration expansion only when a term is selected; keep the default result table readable.
   - Render selected symmetry-adapted orbitals and connect them to the visual orbital objects.

10. **Build the rich result presentation**
    - Implement a controlled Markdown subset with inline/block LaTeX, headings, lists, tables, code, and mathematical superscripts/subscripts.
    - Render labels such as `³A₂` clearly in panels, tables, and scene annotations.
    - Reuse existing font/renderer infrastructure and avoid a full notebook implementation.
    - Keep computed mathematics separate from visual display parameters.

11. **Defer lower-priority workflows**
    - Keep FERWE/FERDO generation as a later phase after states and multiplet analysis are stable.
    - Keep the diffusion path generator as a later module: target atom, path, step count, periodic wrapping/unwrapping, and output files are not part of this implementation.
    - Preserve the future design: unwrapped positions for continuous visualization and wrapped positions for standard structure output.

## Key decisions & tradeoffs

- **Visual foundation before scientific workflow:** paths, arrows, planes, bonds, orbitals, styles, labels, and the Properties Panel come first so group-theory results have a usable presentation layer.
- **One object, two presentations:** scene and diagram views are representations of the same object, not duplicated objects.
- **Standalone plus semantic:** every visual object can be independently edited; links to atoms, structures, operations, and calculations are optional.
- **Style presets plus overrides:** presets enforce consistency while local overrides support publication figures and special cases such as NV⁻.
- **Piecewise Bézier paths:** quadratic/cubic Bézier segments give deterministic, editable spline-like curves.
- **Renderer-local first, persistence second:** use the existing SceneArrow architecture and postpone project serialization until the shared annotation model is stable.
- **`groupy` is authoritative for group theory:** Defect Studio integrates it through jobs/bridge and presents its exact/numeric results; it does not duplicate its character tables, reduction, projection, or multiplet engine.
- **Automatic labels with explicit overrides:** irreps are computed, not guessed; manual overrides are possible only as visibly marked assumptions.
- **Illustrative versus computed:** visual orbital presets may be stylized, but computed WAVECAR/groupy provenance and limitations remain visible.
- **NV⁻ first:** the NV tutorial provides the concrete end-to-end acceptance case and a natural route to general defects later.
- **No FERWE/FERDO yet:** file generation follows analysis stabilization, not the first group-theory UI.

## Risks / open questions

- The exact bridge payloads and serialization contract between Defect Studio and `groupy` still need to be designed and tested against symbolic and numeric paths.
- Geometry-derived representations require robust operation-to-component mapping, including orbital orientation and phase conventions.
- Mixing procedural orbitals and WAVECAR grids is visually useful but may not be a mathematically normalized basis; the UI must state this clearly.
- Persistent object ownership and migration remain a separate stage after renderer-local rework.
- Rich LaTeX/Markdown rendering must reuse available dependencies or add the smallest justified renderer; no full notebook engine is intended.
- `LRthesis.pdf` was located at `C:\Users\fzabi\Desktop\master-theisis\03_PDF_Library\LRthesis.pdf`, but its text extraction is currently blocked by the local MiKTeX PDF utility initialization. Appendix/state conventions remain to be cross-checked before implementing FERWE/FERDO or any thesis-specific state export.
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
