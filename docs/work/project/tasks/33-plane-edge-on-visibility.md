# Task 33: a plane seen edge-on disappears

Queued 2026-09-18 from user testing. Not started, no implementation attempted.

## The report

"plane nie ma grubości, w sensie jak patrzy się na niego z boku to go w ogóle nie widać. Nie jestem
pewien czy tak powinno być. Wydaje mi się, że powinno go być widać ale bez grubości jako
prostopadłościanu."

So: the user is not asking for a slab. They want the plane to remain *locatable* from every angle
without it becoming a box with a thickness they then have to think about.

## Why it happens

`ScenePlane` is a quad - `halfExtents`, `normal`, `tangent`, drawn by
`src/Renderer/OpenGl/OpenGlScenePlaneRenderer.cpp`. A quad viewed exactly along its own plane
covers zero pixels, so it vanishes. This is correct behaviour for a mathematical plane and wrong
behaviour for a thing the user is trying to position.

## Options, cheapest first

1. **Always draw the border, with a minimum screen-space width.** `ScenePlane::showBorder` already
   exists and already draws the outline of the quad. Give that outline a floor of about a pixel or
   two in screen space, and an edge-on plane collapses into a visible line rather than nothing - the
   line *is* the plane seen edge-on, which is the honest picture. No new geometry, no new field, and
   it composes with the existing translucent fill. This is the one to try first.
2. **A thin extruded slab.** Gives a real silhouette from any angle but makes the plane a box: the
   user then sees two faces at grazing angles, has a thickness to tune, and a "plane" that is not
   one. The user explicitly said they do not want this.
3. **Screen-space thickening of the fill** - expand the quad perpendicular to its own plane by
   whatever keeps it a minimum number of pixels wide. Looks right from every angle but the geometry
   no longer matches the numbers in the properties panel, which is a bad trade for a measurement
   tool.

Option 1 unless it turns out the border pass cannot reach a minimum width without a geometry shader
or a line-quad expansion; then reconsider, and say what you found.

## Open question

Does an edge-on plane need to stay *pickable*? A one-pixel line is hard to click. The existing
picking path for planes should be checked - if it tests against the quad, an edge-on plane becomes
unselectable from the viewport even once it is visible, and the outliner row is then the only way
to reach it. That may be acceptable; decide deliberately rather than by accident.
