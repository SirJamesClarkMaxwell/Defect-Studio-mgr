# Decoration Bevel Corner Design

## Goal

Make positive-bevel, positive-thickness `Flat` paths round the actual outline of every endpoint
decoration without spikes, detached patches, or diagonal chamfers across the decoration face. The
result should match the visual rule in `docs/figs/reference.jpg` and `docs/figs/reference2.jpg`:
convex corners round outward, concave shoulders form inward fillets, and acute features retain their
silhouette through a locally reduced bevel.

All eight `PathDecorationKind` values participate in the test matrix. A decoration that cannot form
a valid bevelled solid must remain sharp while the rest of the path stays valid; unsupported input
must never select the old swept-cross-section bevel for the decoration.

## Scope

This work covers:

- `Arrow`, `Stealth`, `Latex`, `Bar`, `Circle`, `Square`, `Diamond`, and `Kite` at both path ends;
- filled, thick `Flat` geometry with `ribbonBevel > 0`;
- convex, concave, acute, and nearly collinear outline vertices;
- local bevel-radius limiting;
- deterministic geometry tests and an OpenGL visual gallery;
- the existing `Shade smooth` option after the final positions and winding are correct.

This work does not change `PathStrokeStyle`, persisted scene data, `Round` paths, zero-thickness
ribbons, or the shape definitions returned by `BuildDecorationContour`.

## Observed Failure and Root Cause

The current solid route is the correct foundation: it constructs a shared shaft-decoration mesh and
bevels canonical faces, edges, and vertices. It already produces the desired plain cuboid and avoids
the old diagonal decoration chamfers.

The remaining failure is local corner interpretation. `PathSolidBeveler` treats every three-edge
vertex as the same convex trihedral corner, uses one global bevel distance, and derives a corner
patch from an unsigned sum of incident normals. That assumption is valid for a cuboid but false for
decoration silhouettes:

- the shaft-width shoulders of an attached arrow are reflex vertices;
- acute tips need a smaller inset distance than the requested global radius;
- sampled `Latex` and `Circle` contours contain shallow turns that must remain a continuous chain;
- `Stealth` has an open back and cannot be assumed to be a closed attached decoration.

At a reflex vertex the current patch is emitted on the convex side, producing the spherical bumps
visible in `bevel3.png` and `bevel4.png`. At an acute vertex the global face inset can pass the next
corner, producing long spikes and disconnected-looking triangles.

## Chosen Architecture

Retain the shared `ThickFlatMesh` and repair the solid beveller. This is smaller and safer than a
second decoration-only offset engine, and it keeps one source of truth for shaft and decoration
seams.

The bevel pass gains a preprocessing stage that derives signed local corner data from the validated
manifold mesh. No decoration-kind-specific geometry is introduced.

### Local corner analysis

For every source face corner, calculate the signed turn relative to the face normal. For every
bevelled edge, calculate the signed dihedral turn using a direction taken from one incident face's
winding. These signs classify the local topology as:

- **convex**: the bevel removes an outside corner;
- **concave/reflex**: the bevel removes material into an inside corner;
- **smooth**: the signed turn is below a scale-relative tolerance;
- **ambiguous**: the incident topology or signs do not describe a supported local patch.

A trihedral source vertex is convex only when its ordered incident turns describe the existing
convex case. A vertex with a reflex outline turn receives a re-entrant patch. Smooth vertices do not
receive an independent spherical pole. Ambiguous vertices keep their local source corner sharp.

### Local radius limiting

The requested radius remains globally capped by half the ribbon width and half the ribbon
thickness. It is then capped per source vertex.

For each incident face, compute how far an inset of radius `r` advances along the two adjacent face
edges. Reduce `r` until neither advance exceeds 45 percent of its available non-degenerate edge.
The vertex radius is the minimum valid radius across its incident faces. This formulation handles
acute convex and reflex angles without a kind-specific constant.

An edge profile may have different effective radii at its two endpoints and therefore may taper.
Its samples reuse the fixed spherical directions established by the circular profile and change
only their radial distance for `ribbonBevelShape`, preserving the current radial-motion contract.

If the remaining radius is below the positional tolerance, that vertex or edge is sharp.

### Convex and re-entrant profiles

Edge strips use the signed dihedral classification to select the convex or re-entrant side of the
profile. The profile endpoints are still the adjacent inset-face points, so neighbouring face and
edge geometry share exact positions.

Vertex patches are built from the already generated ordered boundary arcs:

- a convex vertex uses the outward spherical/superellipsoidal patch;
- a reflex vertex uses the same boundary samples with the radial frame oriented toward the
  re-entrant centre;
- both variants reuse boundary vertices exactly instead of reconstructing them;
- final triangle winding is selected against the expected outward solid normal before normals are
  stored.

The triangular lattice introduced for cuboid corners remains. The change is its oriented frame and
per-vertex radius, not another topology.

### Nearly collinear sampled contours

Scale-relative duplicate points are removed before adjacency construction. A shallow signed turn
does not create a separate vertical corner bevel or vertex patch. Adjacent strips meet at their
shared boundary and `Shade smooth` may average their coincident normals. This prevents sampled
`Circle` and `Latex` outlines from becoming a row of visible spherical beads.

## Applicability and Fallback

Every decoration is rendered in the gallery, but bevel eligibility is structural:

| Decoration | Filled closed-solid expectation | Special condition |
| --- | --- | --- |
| Arrow | Supported | Reflex shaft shoulders and acute tip |
| Stealth | Limited | Open back; bevel only a manifold attached region, otherwise keep decoration sharp |
| Latex | Supported | Shallow sampled turns treated as smooth |
| Bar | Supported | Radius limited by its short axial extent |
| Circle | Supported | Sampled outline treated as a smooth chain |
| Square | Supported | Ordinary convex and attachment corners |
| Diamond | Supported | Acute front and rear points locally limited |
| Kite | Supported | Asymmetric acute points locally limited |

The solid decoration bevel is disabled when the decoration is hollow, the profile is not `Flat`,
thickness is non-positive, or the relevant input component is not a two-manifold. An ineligible
decoration is emitted with a local style copy whose decoration bevel is zero; the shaft may still
use its valid solid bevel. This explicitly prevents a fallback to the old chamfered cross-section
decoration.

Before output is accepted, validate that every non-degenerate undirected edge has two triangle
references and that all positions and normals are finite. A failed bevel component is regenerated
sharp. Renderer code remains exception-free.

## Data Flow

1. `BuildDecorationContour` supplies the unchanged axial decoration description.
2. `BuildAttachedDecorationLoop` creates the mirrored outline and the shaft attachment edge.
3. `ThickFlatMesh` welds shared positions and records which exterior edges request bevel.
4. Bevel preprocessing classifies signed turns and computes effective vertex radii.
5. Face insets, signed edge strips, and oriented vertex patches reuse the same local data.
6. The result is validated and appended by face owner into the existing stroke ranges.
7. `Shade smooth`, when enabled, changes coincident normals only after topology validation.

## Tests

### Geometry tests

The focused matrix covers all eight decorations, both endpoints, and shapes `0.25`, `0.5`, and
`0.75`. It includes a normal bevel and an intentionally excessive bevel to exercise local limiting.

Each supported result must prove:

- all vertex fields and indices are finite and in bounds;
- every non-degenerate undirected edge has exactly two triangle references;
- non-degenerate triangles have outward winding consistent with their normals;
- no emitted point leaves the original sharp solid's thickness bounds or 2D silhouette;
- convex and reflex corner samples lie on their expected side of the source outline;
- increasing the requested radius never moves a locally capped point past its safe limit;
- `ribbonBevel == 0` retains the existing sharp output;
- unsupported decorations do not contain swept cross-section chamfers.

The convex/reflex and local-radius assertions are independent of rendered pixels and are the hard
regression gate for the failures visible in the supplied screenshots.

### Visual gallery

Extend `GlPathRenderTests.cpp` using its existing off-screen context and `WriteVisualArtifact`.
One deterministic gallery places one path per decoration kind in each of three shape columns. The
same kind is used at both endpoints so start/end winding appears in one row. The test writes:

- `path-decoration-bevel-gallery.png` for shaded geometry;
- `path-decoration-bevel-gallery-mesh.png` with the diagnostic mesh overlay.

The gallery asserts non-empty coverage and that sharp and bevelled renderings differ. It is a
single inspection surface rather than eight manually created scenes; structural geometry tests,
not driver-sensitive pixel equality, remain the automatic correctness oracle.

## Acceptance Criteria

- The supplied failure scenes contain no outward bubbles at reflex shoulders and no tip spikes.
- Convex, reflex, and acute behaviour is driven by geometry, not `PathDecorationKind` branches.
- All eight decorations appear in both visual artifacts at both path orientations.
- Unsupported cases remain visibly sharp and structurally valid.
- No decoration uses the old swept-cross-section bevel fallback.
- The focused new tests pass; the pre-existing mesher suite gains no new failures.
- Debug application and Release tests build successfully, followed by `graphify update .`.
