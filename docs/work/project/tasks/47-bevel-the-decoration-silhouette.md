# Bevel the decoration's silhouette edges, not the swept cross-section's corners

Repository: DefectStudio, C++23, premake5 + MSBuild on Windows. Renderer layer is exception-free.
`.cpp` files stay under ~500 lines. Read `AGENTS.md` and `CLAUDE.md` before anything else.

## The goal, stated by the user with reference images

A path can be drawn as a "thick Flat ribbon": a flat strip of a given `width` extruded to a given
`ribbonThickness`, swept along the path, with an endpoint decoration such as an arrowhead. The user
wants Blender's bevel on it: the edges of the resulting SOLID rounded off, including the edges that
run around the arrowhead's outline, with `ribbonBevelSegments` faces per edge and a profile
controlled by `ribbonBevelShape`.

The user marked their screenshots precisely:
- the chamfers that currently appear run diagonally ACROSS the arrowhead's face, along the sweep
  direction. They are wrong and should not exist.
- the edges that should be bevelled are the ones tracing the arrowhead's OUTLINE - its two slanted
  sides and its base - plus the long edges of the shaft.

## Why the current implementation cannot do it

`src/Renderer/Path/PathDecorationMesher.cpp` builds a rectangular CROSS-SECTION, chamfers its four
corners (`ribbonBevelSegments` faces each, along a superellipse profile matching Blender's
convention: 0 concave, 0.5 circular, 1 out to the sharp corner), and sweeps that ring along the
path. `CrossSectionRingSize` is the single source of how many vertices a ring has.

For the shaft this is correct: four chamfered corners sweep into the four long edges of the ribbon.

For a decoration it cannot be correct. An arrowhead is not a swept cross-section - it is a solid
with its own outline. Sweeping a chamfered ring through it produces facets along the sweep
direction, which is exactly the diagonal chamfer the user marked in red. There is no "edge around
the arrowhead's outline" in this model for a beveller to find.

## What has already been tried, and precisely how it failed

An attempt was made to rebuild the thick Flat stroke as one closed solid and bevel its edges. The
design is in `docs/work/project/plans/2026-09-27-thick-ribbon-as-a-solid.md`. It was reverted; the
code is preserved outside the tree and can be recovered if useful, but do not assume it is a good
starting point.

Measured outcomes of that attempt, in order:

1. The solid closed the PLAIN end caps. A whole-mesh closure test over strokes with no decorations
   passed on it, and the black shading at collapsed decoration tips disappeared by construction,
   because degenerate tip faces stopped being emitted.
2. With decorations, the solid did NOT close. The 96-case decoration matrix kept failing even with
   the bevel pass disabled, so the solid itself was incomplete around the shoulder where the shaft
   meets a wider decoration.
3. The bevel pass never closed. Four rounds moved it from "an edge used by one triangle" (a hole) to
   "an edge used by four" (a duplicate). The duplicate survived deduplicating the undirected-edge
   enumeration and was last seen on the cap plane.

The bevel pass was written as the standard construction - one shrunk face per input face, one
profile strip per undirected edge, one patch per vertex, each emitted exactly once from an explicit
enumeration. That construction is believed right in shape; its iteration was not.

## What is wanted from you

A root-cause answer, then an implementation:

1. Decide whether the closed-solid-then-bevel route is the right one, or whether there is a better
   construction for this specific geometry - for example generating the decoration as a solid
   directly with its outline edges known, rather than recovering them from a mesh afterwards.
2. If the solid route stands: say exactly why the duplicate-edge fault survived a deduplicated
   enumeration. "Emit each element once" should make closure structural, so something about the
   input mesh - shared vertices between shaft and decoration, degenerate faces, a non-2-manifold
   shoulder - is breaking the assumption. Name it.
3. Implement it so that:
   - the arrowhead's outline edges are bevelled and no chamfer runs across its face;
   - `ribbonBevel == 0` produces geometry identical to today;
   - every non-degenerate undirected edge of the result is used by exactly two triangles;
   - triangle winding agrees with the vertex normals it carries (a flipped winding must flip the
     normals with it);
   - `StrokeProfile::Round` is untouched - a tube has no edges to soften.

## How the work is judged

`tests/Renderer/Path/PathStrokeMesherTests.cpp` holds the closure matrix, a no-decoration whole-mesh
closure case, a bevel profile monotonicity case, and an orientation assertion
(`AssertRangeTrianglesFaceOutward`) enabled for thick Flat strokes. Build and run them:

    Vendor\Binaries\Premake\Windows\premake5.exe vs2022
    MSBuild build\generated\vs2022\DefectStudioTests.vcxproj /p:Configuration=Release /p:Platform=x64 /m:2 /nr:false /v:minimal
    build\bin\Release-windows-x86_64\DefectStudioTests\DefectStudioTests.exe --gtest_filter=PathStrokeMesherTests.*

MSBuild is at `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`.
Two tests in the full suite are permanently skipped (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
`BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`); that is expected.

## A known unrelated defect, so you do not chase it

With a decoration at either end, the band of SHAFT adjacent to that decoration is wound inside out,
and the decoration inverts with it. That case is currently skipped in the orientation assertion,
with the failure counts written beside it (596 when first enabled, 572 after a back-closure fix, 607
for an attempt that was reverted). It is a separate fault from the bevel. If your change happens to
fix it, say so; do not let it distract you.

## Constraints

- Layer boundaries in `AGENTS.md` are hard. `Renderer` may not throw.
- Do not change `src/Renderer/Path/PathStyle.hpp` - it is the contract for the style fields.
- Prefer deleting the swept-chamfer code to extending it, if the new construction replaces it.
- If you conclude the goal is not reachable without a change the user must approve - a file format
  change, a new dependency, a rewrite of how decorations are authored - stop and say so rather than
  doing it.
