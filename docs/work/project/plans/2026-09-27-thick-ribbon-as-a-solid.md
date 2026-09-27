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
