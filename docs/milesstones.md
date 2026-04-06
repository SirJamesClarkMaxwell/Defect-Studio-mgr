# MILESTONES – DefectsStudio

> Release-oriented milestone tracking for DefectsStudio.  
> This document is complementary to `TODO.md`: TODO remains the detailed execution backlog, while this file tracks larger release-shaped milestones and their Definitions of Done.

## Milestone Policy

- Each milestone is tied to a release label.
- Checkboxes represent the practical Definition of Done for the milestone.
- Milestones are workflow-oriented and release-oriented, not a copy of the TODO backlog.
- The MVP target is **R7 / Scientific MVP 1.0**.
- Post-MVP milestones continue beyond Scientific MVP 1.0 and track platform expansion.

---

## M1 — Foundation / Developer Platform Ready
**Release:** R0 / Internal Foundation Snapshot  
**Mapping:** T01, T02, T03, T04, T05

- [ ] Debug and Release builds work reliably
- [ ] Windows build path is stable
- [ ] Linux build path is validated or reproducible
- [ ] `Application`, `LayerStack`, and event loop are operational
- [ ] `ProjectWorkspace` skeleton exists
- [ ] Python Scientific Runtime starts correctly
- [ ] At least one Python integration roundtrip test passes
- [ ] Capability reporting exists in a first practical form
- [ ] Base documentation exists in mdBook
- [ ] Local CI/check script exists
- [ ] Sample files for basic regression testing exist
- [ ] Log panel and job/task monitor exist in the normal UI

**Release outcome:** the project has a stable engineering base for further work.

---

## M2 — First Structure Workflow End-to-End
**Release:** R1 / Structure Editor Preview  
**Mapping:** T06, T07, T08

- [ ] POSCAR import works
- [ ] At least one additional structure format works (`CIF` or `XYZ`)
- [ ] Imported structures become valid domain objects
- [ ] Structures render correctly in the viewport
- [ ] Atom selection works
- [ ] Basic structure editing works
- [ ] Distance and/or angle measurement works
- [ ] Undo/redo works for structure and scene edits
- [ ] Initial clipboard support exists in a useful scope
- [ ] Initial drag/drop support exists in a useful scope
- [ ] Parser tests exist for the initial supported formats

**Release outcome:** the application supports a meaningful first structure workflow.

---

## M3 — Project-Centric Workbench
**Release:** R2 / Project Workbench Alpha  
**Mapping:** T09, T10, T11

- [ ] `Create Project` works
- [ ] `Open Project` works
- [ ] `Recent Projects` works
- [ ] Canonical save works
- [ ] Project recovery/autosave works
- [ ] Workspace/UI autosave works
- [ ] Core project state survives reopen
- [ ] Scene-related state is persisted at a practical level
- [ ] Relative-path handling works
- [ ] Missing-file validation on open works
- [ ] User-default and project-level layout/settings behavior work
- [ ] Feature availability is surfaced through the capability model

**Release outcome:** DefectsStudio behaves as a real project-oriented workbench rather than a session-only prototype.

---

## M4 — Native Structure Authoring and Scientific Records Foundation
**Release:** R3 / Structure Authoring Alpha  
**Mapping:** T12, T13

- [ ] New Structure wizard works
- [ ] Cell definition by lattice parameters works
- [ ] Cell definition by explicit lattice vectors works
- [ ] Primitive/conventional workflow works
- [ ] Supercell workflow works
- [ ] Prototype library exists in a first useful scope
- [ ] Structures created from scratch can be saved and reopened
- [ ] Structures created from scratch render correctly after reopen
- [ ] `ProjectWorkspace` exists as the runtime container for an open project
- [ ] Project-scoped registries exist for structures, defects, configurations, calculations, and analyses
- [ ] `DefectConfiguration`, `CalculationRecord`, and `AnalysisRecord` exist in a practical first runtime form

**Release outcome:** the application supports native structure creation and gains an explicit scientific-records runtime model.

---

## M5 — Volumetric Visualization & Export
**Release:** R4 / Volumetrics Preview  
**Mapping:** T14, T15

- [ ] `CHG`, `CHGCAR`, or `PARCHG` parsing works in a first practical scope
- [ ] Scalar-field domain model exists
- [ ] At least one isosurface workflow works end-to-end
- [ ] Volumetric visualization controls work
- [ ] Request/commit GPU execution path works
- [ ] Offscreen export to image works
- [ ] Volumetric derived outputs are managed separately from lightweight project state
- [ ] At least one regression dataset exists for volumetric workflows

**Release outcome:** the workbench can handle and export meaningful volumetric workflows.

---

## M6 — VASP Workflow Integration
**Release:** R5 / VASP Workflow Alpha  
**Mapping:** T16, T17

- [ ] At least one convergence workflow exists
- [ ] KPOINTS workflow exists in a first useful scope
- [ ] OUTCAR metadata parsing works
- [ ] WAVECAR metadata path works
- [ ] `CalculationRecord` is meaningful in the runtime model
- [ ] Parsed results are storable in a reproducible way
- [ ] The workflow reduces manual glue-script usage for at least one practical VASP scenario

**Release outcome:** the application becomes a practical front-end for core VASP-oriented workflows.

---

## M7 — Defect Thermodynamics MVP
**Release:** R6 / Defect Thermodynamics Alpha  
**Mapping:** T18

- [ ] Formation energy workflow works for at least one meaningful case
- [ ] First practical correction workflow exists
- [ ] CTL workflow exists
- [ ] Binding-energy output exists
- [ ] Equilibrium concentration output exists in a first useful scope
- [ ] Results can be persisted
- [ ] Results can be exported
- [ ] The implementation remains compatible with later GPU FNV integration
- [ ] Scientific errors are surfaced without destabilizing the application

**Release outcome:** the application provides the first major high-value scientific analysis module.

---

## M8 — Optics & Phonons MVP Closure
**Release:** R7 / Scientific MVP 1.0  
**Mapping:** T19

- [ ] ZPL workflow exists in a practical first form
- [ ] Huang-Rhys / DWF workflow exists
- [ ] CCD workflow exists
- [ ] Experimental/theoretical spectrum overlay exists
- [ ] Results remain exportable and project-bound
- [ ] Module boundaries remain intact
- [ ] Scientific MVP workflows can be demonstrated end-to-end on reference datasets

**Release outcome:** DefectsStudio reaches **Scientific MVP 1.0**.

---

## M9 — Post-MVP Productization and Packaging
**Release:** R8 / Post-MVP Foundation  
**Mapping:** post-MVP foundation work

- [ ] Portable distribution is polished
- [ ] Installer work may begin if justified
- [ ] Feature-gate and product policy groundwork are expanded if needed
- [ ] Calculation packaging / scientific bundle tooling may begin as a technical subsystem

**Release outcome:** the project starts post-MVP productization and tooling expansion.

---

## M10 — Diffusion Analysis
**Release:** R9 / Diffusion Preview  
**Mapping:** T21

- [ ] Diffusion analysis scope is defined and implemented in a first usable form
- [ ] Input and output models are represented in the domain
- [ ] Results are persistable in the project
- [ ] At least one diffusion workflow works end-to-end

**Release outcome:** the platform expands into diffusion-oriented workflows.

---

## M11 — Defect Template Generation
**Release:** R10 / Template Generation Alpha  
**Mapping:** T22

- [ ] Defect template format exists
- [ ] Template library exists in a first scope
- [ ] Expansion into multiple variants works
- [ ] Naming and manifest generation work
- [ ] Batch export works
- [ ] Dry-run preview exists

**Release outcome:** the platform supports repeatable generation of defect families from templates.

---

## M12 — Symmetry & Space Group Analysis
**Release:** R11 / Symmetry Alpha  
**Mapping:** T23 + space-group scope

- [ ] Point-group or space-group identification works in a first practical scope
- [ ] Space-group browser exists
- [ ] Cell standardization works
- [ ] Wyckoff/site-symmetry information works in a first scope
- [ ] At least one useful symmetry-analysis workflow works end-to-end

**Release outcome:** symmetry becomes a first-class scientific workflow area.

---

## M13 — GPU Orbital Projection
**Release:** R12 / GPU Orbital Engine Preview  
**Mapping:** `T23 - GPU Orbital Projection`

- [ ] GPU orbital projection component exists in a first working form
- [ ] Input contracts from WAVECAR/INCAR/KPOINTS are explicit
- [ ] Output contracts toward orbital/isosurface workflows are explicit
- [ ] The component can be integrated without breaking the main architecture
- [ ] At least one reference workflow runs successfully

**Release outcome:** specialized GPU-assisted orbital workflows become available.

---

## M14 — Remote Workbench
**Release:** R13 / Remote Alpha  
**Mapping:** T24

- [ ] Remote server management works
- [ ] SFTP browsing works
- [ ] Upload/download works
- [ ] Basic remote shell works
- [ ] Basic job-submission helper works
- [ ] Large-file transfer has a practical compression-aware workflow
- [ ] Secure credential handling exists in a practical first form

**Release outcome:** the platform supports the first meaningful remote-cluster workflows.

---

## M15 — Defect Database
**Release:** R14 / Database Alpha  
**Mapping:** T25

- [ ] Local defect database schema exists
- [ ] Own results can be added
- [ ] Search and filtering work
- [ ] At least one external source can be imported
- [ ] Export works in at least one machine-readable format
- [ ] Database workflows integrate cleanly with project workflows

**Release outcome:** the platform supports structured defect-result management.

---

## M16 — Quantum Emitter Evaluation
**Release:** R15 / Emitter Scorecard Preview  
**Mapping:** T26

- [ ] Emitter scorecard exists
- [ ] Radar-chart or equivalent comparative visualization exists
- [ ] At least one classification workflow exists
- [ ] At least one helper calculator exists
- [ ] Results can be tied back to project or database context

**Release outcome:** the platform supports comparative emitter evaluation.

---

## M17 — Orbital & Phonon Visualization
**Release:** R16 / Visualization Alpha  
**Mapping:** T27

- [ ] Orbital visualization exists in a first practical scope
- [ ] Phonon animation exists
- [ ] Playback controls exist
- [ ] Export of image sequence or equivalent output works
- [ ] The workflow is usable on at least one reference project

**Release outcome:** the platform supports advanced scientific visualization workflows.

---

## M18 — Spin & Magnetic Properties
**Release:** R17 / Spin Physics Preview  
**Mapping:** T28

- [ ] Spin/magnetic data model exists in a first practical scope
- [ ] At least one major workflow works (`ZFS`, `g tensor`, or hyperfine)
- [ ] Results are visualized or analyzed in a useful way
- [ ] Results are persisted in project context

**Release outcome:** the platform expands into spin and magnetic-property workflows.

---

## M19 — Knowledge Integration
**Release:** R18 / Knowledge Tools Alpha  
**Mapping:** T29

- [ ] Minimal Zotero or Obsidian integration works
- [ ] Citation export or note generation works
- [ ] Project/domain objects can be linked to knowledge artifacts
- [ ] The integration does not break the main scientific workflow model

**Release outcome:** the platform gains practical knowledge-management support.

---

## M20 — Calculation Packaging / Scientific Bundles
**Release:** R19 / Scientific Bundles Alpha  
**Mapping:** T30

- [ ] Typed scientific bundle envelope exists with shared manifest and provenance
- [ ] Bundle payloads are typed by workflow/calculation profile
- [ ] Bundle import path exists as an additional project import path
- [ ] Bundle handling remains a tooling/import concern rather than replacing the main domain model
- [ ] Reader/writer boundary is clean enough to become a future technical library if justified

**Release outcome:** the platform gains a post-MVP scientific packaging/import subsystem without disrupting the main project model.

---

## Release Notes Policy

- `R0` is an internal foundation snapshot.
- `R1–R6` are staged feature releases leading toward MVP.
- `R7` is **Scientific MVP 1.0**.
- Later releases extend the platform beyond MVP rather than redefining MVP itself.
- Patch and stabilization releases after a milestone (for example `R7.1`) may exist without introducing a new product milestone.

---

## Validation Rule

A milestone should only be considered complete when:

- its checkbox-based Definition of Done is satisfied
- at least one real end-to-end workflow works
- the result is testable or demonstrable on a reference dataset
- architectural boundaries remain intact
- the release is usable for its intended scope
