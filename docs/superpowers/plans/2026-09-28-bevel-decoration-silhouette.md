# Decoration Silhouette Bevel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bevel the actual solid outline of thick Flat endpoint decorations without sweeping a chamfered cross-section across their faces.

**Architecture:** Keep the current meshing path unchanged for `ribbonBevel == 0`, zero-thickness Flat strokes, and `StrokeProfile::Round`. For positive-bevel thick Flat strokes, first assemble a sharp 2-manifold solid whose shaft/decorations share explicit handoff topology, then bevel that solid with one face inset, one strip per undirected edge, and one non-overlapping patch per vertex.

**Tech Stack:** C++23, GLM, GoogleTest, premake5, MSBuild.

**Spec:** `docs/work/project/tasks/47-bevel-the-decoration-silhouette.md`

## Global Constraints

- Renderer code remains exception-free.
- Do not change `src/Renderer/Path/PathStyle.hpp`.
- Keep every `.cpp` file under roughly 500 lines.
- Preserve the exact existing output when `ribbonBevel == 0`.
- Leave `StrokeProfile::Round` unchanged.
- Every non-degenerate output edge must have exactly two triangle references.
- Triangle winding and carried vertex normals must agree.

---

### Task 1: Lock down the reported geometry failure

**Files:**
- Modify: `tests/Renderer/Path/PathStrokeMesherTests.cpp`

**Interfaces:**
- Consumes: `BuildStroke(const EvaluatedPath&, const PathStrokeStyle&)`.
- Produces: regression coverage for decorated whole-mesh closure and an inset silhouette vertex that the swept-ring implementation cannot emit.

- [x] Add a filled Arrow fixture matching the thick Flat bevel configuration.
- [x] Assert the complete decorated result is closed by position, not only the decoration subrange.
- [x] Assert a circular-profile bevel removes the exact sharp tip and emits vertices inset in both axial and lateral silhouette directions.
- [x] Run the focused tests and confirm they fail for the expected open handoff/swept-cross-section reasons.

### Task 2: Build one sharp manifold before beveling

**Files:**
- Create: `src/Renderer/Path/PathSolidMesher.hpp`
- Create: `src/Renderer/Path/PathSolidMesher.cpp`
- Modify: `src/Renderer/Path/PathStrokeMesher.cpp`
- Modify: `src/Renderer/Path/PathDecorationMesher.cpp`

**Interfaces:**
- Produces: an internal indexed polygon mesh for positive-bevel thick Flat geometry, with explicit face ownership and shared shaft-decoration handoff vertices.
- Preserves: the legacy path for zero bevel and all non-target profiles.

- [x] Add a sharp-solid builder selected only for finite positive bevel on positive-thickness Flat strokes.
- [x] Emit shaft and decoration faces without independent overlapping caps at decorated handoffs.
- [x] Split decoration back edges at shaft-width attachment points and share those four junction vertices with the shaft boundary.
- [x] Reject degenerate faces before adjacency construction.
- [x] Emit the sharp solid temporarily and run the whole-mesh closure regression until it passes.
- [x] Confirm zero-bevel geometry still matches the legacy output byte-for-byte.

### Task 3: Bevel faces, edges, and vertices without overlapping corner profiles

**Files:**
- Create: `src/Renderer/Path/PathSolidBeveler.cpp`
- Modify: `src/Renderer/Path/PathSolidMesher.hpp`

**Interfaces:**
- Consumes: the validated 2-manifold sharp mesh from Task 2.
- Produces: `StrokeGeometry` using one shrunk face per source face, one profile strip per canonical undirected edge, and one patch per source vertex.

- [x] Build adjacency and fall back to the sharp mesh when an input edge does not have exactly two incident faces.
- [x] Add endpoint trimming/miter anchors so profiles of distinct incident edges never retrace the same face segment at `ribbonBevelShape == 0`.
- [x] Emit edge profile strips using `ribbonBevelSegments` and the existing superellipse convention.
- [x] Emit one ordered vertex patch and deduplicate coincident boundary positions before triangulation.
- [x] Generate normals from each emitted face after final winding is chosen.
- [x] Run closure, profile monotonicity, silhouette, and orientation tests after each minimal change.

### Task 4: Integrate and verify the complete path contract

**Files:**
- Modify: `tests/Renderer/Path/PathStrokeMesherTests.cpp`
- Modify only if required: `src/Renderer/Path/PathStrokeMesher.cpp`

**Interfaces:**
- Verifies: all style combinations covered by `PathStrokeMesherTests.*`.

- [x] Extend the decoration matrix to check the complete thick Flat beveled result, not isolated overlapping subranges.
- [x] Verify `ribbonBevelSegments` changes tessellation and `ribbonBevelShape` changes the profile without doubling back.
- [x] Regenerate Visual Studio projects because new `.cpp` files are added.
- [x] Build Release tests and run `PathStrokeMesherTests.*`.
- [x] Run `git diff --check` and `graphify update .`.
