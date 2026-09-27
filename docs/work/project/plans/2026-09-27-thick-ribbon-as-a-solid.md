# The thick Flat ribbon becomes a solid, then gets bevelled

Decided 2026-09-27 with the user: the bevel is to work like Blender's, on the whole solid, not only
on the four long edges of a swept cross-section.

## Why this is one change and not two

Three defects reported over the last manual rounds share a cause:

- the tip sits slightly offset from the end of the shaft (a visible step);
- the arrowhead reads as not closed;
- the bevel only softens the shaft's long edges, so a decoration keeps sharp silhouette edges and
  a crease, and nothing is rounded where three faces meet.

The first two are the documented T-junction in the shaft/decoration handoff. That handoff shares
position and frame but deliberately NOT half-width, because sharing the width flared the ribbon
into a funnel - that was tried and reverted earlier. So the shaft ends at one cross-section, the
decoration begins at a wider contour, and nothing spans the difference. What is missing is a
shoulder face.

The third needs edges to bevel, which a swept cross-section does not have: we chamfer the PROFILE
and sweep it, so there is no such thing as "the edge round the arrowhead's outline".

Building the whole stroke as one closed solid, and bevelling its edges afterwards, answers all
three. The shoulder becomes an ordinary face of the solid; the decoration's outline becomes an
ordinary edge that the beveller can round.

## The shape of the work

1. **Build the closed solid.** The thick Flat stroke stops being "sweep a bevelled rectangle" and
   becomes: two offset surfaces (front and back, at plus and minus half the thickness), joined by a
   side wall round the whole silhouette, including the decoration outlines and the shoulder where
   the shaft meets a wider decoration. No bevel at this stage - a sharp, closed, correctly wound
   solid. `ribbonBevel == 0` must render exactly as it does today, which is the check that this
   step is right before anything is rounded.
2. **Bevel the solid's edges.** A separate pass over that mesh: find the edges, replace each with
   `ribbonBevelSegments` faces along the Blender superellipse profile already implemented, and
   resolve the corners where three or more bevels meet. The corner cases are the hard part and are
   where Blender's own `bmesh_bevel` is most intricate - and where it too degrades on non-quad
   geometry, which the user named as the likely cause of what they were seeing.
3. **Round profile is untouched.** A tube has no edges to soften; `ribbonBevel` has always been
   Flat-only and stays so.

## What this costs, honestly

Step 1 is a rewrite of the thick-Flat path in `PathStrokeMesher` and `PathDecorationMesher`, and it
deletes the bevelled-cross-section code rather than extending it. `CrossSectionRingSize` stops
being the ring authority for the bevelled case, because there stops being a bevelled ring.

Step 2 is a general mesh operator. It is the part that can expand without limit if the corner
handling is chased to completeness. A defensible first version bevels edges and accepts a simple
mitre at corners, with the ceiling written down.

Do not start step 2 until step 1 renders identically to today at `ribbonBevel == 0` and the closure
matrix - extended with a no-decoration row, which it still lacks - passes over it.

## Sequencing against the open defects

The black decoration tips and the parked `-0.476` orientation lead both live in the code step 1
replaces. Neither should be chased first: a fix landed in the bevelled-cross-section path is work
thrown away. Step 1 either resolves them by construction or reproduces them on ground that can be
reasoned about.

## Status 2026-09-27: attempted in one pass, parked

Built and reverted the same day. The work is preserved in the session scratchpad as
`parked-solid/` - `PathSolidMesher.{hpp,cpp}`, `PathSolidBeveler.cpp` and `mesher-wiring.patch`
against `PathStrokeMesher.cpp` / `PathDecorationMesher.cpp`.

What happened, in order:

1. Step 1 and step 2 were done together, at the user's request. That removed the checkpoint this
   plan asks for, and it cost more than it saved.
2. The solid closed the PLAIN end caps - `NoDecorationMatrixChecksWholeMeshClosure` passed on it -
   and removed the black decoration tips by construction, because collapsed tips stopped emitting
   degenerate faces.
3. The bevel pass never closed. Four rounds moved it from an edge used by one triangle (a hole) to
   an edge used by four (a duplicate), which is progress in diagnosis but not a working mesh. The
   duplicate survived a deduplicated undirected-edge enumeration and was last seen on the cap plane.
4. With the bevel pass disabled, `DecorationMatrixChecksClosedSurfacesAndHandoffFrame` still failed,
   so the solid was not closed with decorations either. That is two unfinished pieces, not one.

### What to do differently

- **Take the checkpoint.** Land the solid with no bevel at all, green, as its own commit. Do not
  start the beveller until then.
- **Write the failing test first, from the user's own configuration.** The extended matrix passes
  against the pre-rewrite code, so it does NOT catch the hole visible on screen - a stroke built by
  the "Thick curved Flat ribbon" dev preset, which is what the screenshots show. Reproduce that
  exact preset in a test and watch it fail before touching geometry again. A closure test that
  passes over a mesh with a visible hole is measuring the wrong stroke.
- **Keep the structural construction.** One shrunk face per face, one strip per undirected edge,
  one patch per vertex is the right shape; the remaining fault is in its iteration, not its idea.
