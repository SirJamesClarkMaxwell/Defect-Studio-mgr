# Task 66: path feedback round (2026-10-04 manual test)

Branch `task/65-feedback-round`. Five path items from the user's manual test. Another agent edits
non-path files in the same working tree at the same time: touch only the files this task needs (path
renderer/mesher/commands/edit-mode/properties) and list every file you change.

## 1. Selected tube draws as orange stripes (rendering)

A selected Round (tube) path shows its selection colour as stripes running along the whole tube, plus
dotted rings at the ends, instead of a clean silhouette outline (user screenshots: the tube is covered
in orange lines). Selection outline: `src/Renderer/OpenGl/OpenGlPathRenderer.cpp` (`u_OutlineMode`,
`u_OutlineExpansion`, drawJob outline pass) and its shaders. Commit 252c58e ("solid path meshing, bevel
repairs and outline depth bias") already fought an outline depth problem; check whether the outline shell
z-fights with the tube (front faces of the expanded shell not culled, or the depth bias/state not applied
to Round tubes), fix the root cause, and keep the outline a silhouette for every profile.

## 2. Bevel: per-part control and the arrowhead artefacts

- "Not everywhere a bevel is wanted": add a style choice for WHERE the ribbon bevel applies - shaft only,
  decorations only, or both (default both = today's look). This needs a new `PathStrokeStyle` field: you
  ARE allowed to add one field to `src/Renderer/Path/PathStyle.hpp` for this (an enum, default "both"),
  persisted in the scene-objects YAML as an optional key (old files load unchanged), and shown next to the
  other bevel controls in the path style UI.
- Arrowhead with bevel: small bumps where the shaft meets the head and dots at the head's corners
  (screenshot: blue Flat ribbon with an Arrow decoration, bevel > 0).
- A Flat ribbon with a Square/Bar-like end decoration and bevel grows SPIKES at the decoration's corners
  and edge midpoints (screenshot: orange plate with triangular spikes sticking out). Task 61 gave crossing
  corners smaller radii; the spikes suggest corner patches whose inset points land outside the face, or a
  wrong reflex/convex classification on that decoration's outline.
Add closure/shape tests that fail before your fix (e.g. no vertex of the bevelled solid lies outside the
unbevelled solid's bounding box by more than epsilon; no triangle normal points into the solid) for the
Arrow and the square decoration, segments 1/4/16, shapes 0/0.5/1.

## 3. Ctrl+R: insert nodes into a path segment (Blender loop cut)

In Path Edit Mode, Ctrl+R with the mouse over a segment starts a modal insert: a preview of N evenly spaced
new nodes on the hovered segment (N starts at 1), mouse wheel changes N (1..32), left click / Enter commits,
right click / Escape cancels. The segment is split so the curve shape is preserved (Line: points on the
line; Cubic: de Casteljau splits; Arc: equal sub-arcs). One undo entry. Route through CommandRegistry +
keymap like the other Path Edit Mode keys (task 48a). Tests for the split maths (shape preserved, N nodes,
undo restores).

## 4. Alt+R must reverse the arrow's direction

Today Alt+R on a path swaps its colours and leaves the arrowhead where it was. Wanted: the arrow points the
other way and its colours stay where they were on screen. I.e. reverse the node/segment order, keep the
start/end decorations attached to start/end (so the head moves to the other physical end), and mirror the
gradient stops (position -> 1 - position, order reversed) so the colours stay physically in place. Same for
the Edit Mode reverse. Update the tests that assert the old behaviour.

## Files that must NOT be touched

`Vendor/**`, `install/users/**`, `src/Domain/**`, anything under `src/Presentation/Panels/Viewport{Defect,
Vacancy,Text}*`, `src/Presentation/Panels/ObjectPropertiesPanelSections.cpp`, label code, defect-frame code
(the other agent's area).

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build; the caller builds
and sends errors. Report: per item the root cause, the fix, files changed, tests added.
