# GATHERING RESULTS – DefectsStudio

> Validation and evaluation guidance for DefectsStudio after milestone and release delivery.  
> This document defines how progress should be judged in practice once functionality exists.

## Purpose

DefectsStudio should not be evaluated only by completed backlog items. The project should be judged by whether it supports real scientific workflows in a stable, correct, and repeatable way.

This document defines how to gather results after milestones and releases, especially around the Scientific MVP 1.0 scope and later platform expansion.

---

## Core Evaluation Principle

The main question is not:

- “Was the task completed?”

The main question is:

- “Can the intended scientific workflow now be performed end-to-end, with correct results and without unnecessary manual workaround scripting?”

This means that release evaluation should focus on **workflow value**, **scientific correctness**, **stability**, and **architectural discipline**.

---

## Evaluation Areas

### 1. Workflow Validation

Each major release should be tested against real end-to-end workflows.

Examples include:

- importing and editing a crystal structure
- creating a structure from scratch
- running a volumetric visualization workflow
- performing a practical VASP-oriented workflow
- computing defect thermodynamics
- running an optics/phonon workflow
- later: symmetry, remote, database, diffusion, and other expansion workflows

A release passes workflow validation when at least one meaningful workflow in its target scope can be completed from input to saved/exported result.

### 2. Scientific Validation

Results must be checked against trusted references.

Examples include:

- known-good sample files
- expected parser outputs
- reference calculations
- previously validated scripts
- literature-consistent values where applicable

Scientific validation should confirm that:

- file parsing is correct,
- transformations preserve meaning,
- exported results are not corrupted,
- Python-backed scientific workflows return expected values or shapes,
- result interpretation remains scientifically sensible.

### 3. Performance and Stability

The application should remain usable on practical project sizes and realistic datasets.

Validation in this area should check:

- whether the application crashes under normal use,
- whether long-running tasks block the UI,
- whether save/load remains stable,
- whether volumetric or VASP-related workflows remain manageable in memory and time,
- whether Debug and Release builds both remain healthy.

The goal is not premature optimization, but practical scientific usability.

### 4. Architectural Conformance

A release is not successful if it delivers features by quietly breaking the architectural model.

Validation should confirm that:

- the domain remains the source of truth,
- ECS remains a derived scene/editor representation,
- Storage and IO remain distinct,
- Scientific Runtime remains behind controlled boundaries,
- renderer code does not absorb scientific logic,
- new features do not bypass agreed module boundaries.

If exceptions are introduced, they should be explicit, limited, and documented.

### 5. Research-Workflow Value

Because the first real users are the project author and then the research group, practical value should be measured by whether the application improves real scientific work.

Indicators include:

- fewer ad-hoc scripts needed outside the application,
- less manual file manipulation,
- easier return to earlier project state,
- faster production of figures and exports,
- more reproducible workflow between sessions,
- lower cognitive load when continuing analysis work.

---

## Reference Assets for Validation

Validation should rely on reusable reference material.

Recommended categories:

- **reference datasets**  
  sample POSCAR, CIF, XYZ, OUTCAR, WAVECAR, CHGCAR/PARCHG, spectra, and project folders

- **reference projects**  
  a small set of representative DefectsStudio project directories used repeatedly across releases

- **expected outputs**  
  saved result files, plots, exported images, or structured result summaries

- **workflow checklists**  
  short repeatable acceptance flows for each milestone or release

These assets should evolve with the project and be reused across milestones rather than recreated ad hoc.

---

## Per-Release Evaluation Questions

After each release, answer the following questions:

- [ ] Does at least one target workflow work end-to-end?
- [ ] Are the results scientifically plausible or reference-matching?
- [ ] Is the release stable enough for its intended use?
- [ ] Does the implementation respect the architectural boundaries?
- [ ] Does this release reduce manual work compared to the previous workflow?

If any answer is “no,” that should lead to a follow-up issue, stabilization task, or architectural review.

---

## Minimal Evidence to Collect Per Milestone

For each major milestone or release, collect at least:

- [ ] one reference dataset or project used for evaluation
- [ ] one workflow acceptance note
- [ ] one short list of known limitations or issues
- [ ] confirmation that Debug and Release builds were checked
- [ ] confirmation that the release outcome matches the intended milestone scope

Optional but recommended:

- [ ] timing notes for important workflows
- [ ] memory observations for heavy workflows
- [ ] export samples
- [ ] screenshots or generated figures
- [ ] notes about architecture exceptions or temporary compromises

---

## Scientific MVP 1.0 Evaluation Focus

For **R7 / Scientific MVP 1.0**, evaluation should focus especially on:

- structure import, editing, and authoring
- project save/reopen continuity
- volumetric visualization and export
- VASP-related parsing and convergence workflow
- defect thermodynamics
- optics and phonons

Scientific MVP 1.0 should be considered successful only if these workflows work on real reference datasets in a way that is stable, scientifically plausible, and practically useful.

---

## Post-MVP Evaluation

After Scientific MVP 1.0, evaluation should continue in the same style for:

- diffusion workflows
- defect template generation
- symmetry and space-group analysis
- GPU orbital projection
- remote/server workflows
- defect database workflows
- scorecard and comparative workflows
- orbital and phonon visualization
- spin and magnetic workflows
- knowledge integration workflows

The same validation model applies: workflow value first, then scientific correctness, then stability, then architectural discipline.

---

## Outcome of This Document

A release or milestone should not be considered “done” merely because code exists. It should be considered done when the intended workflow is demonstrably useful, scientifically credible, stable enough for its scope, and still aligned with the agreed architecture.
