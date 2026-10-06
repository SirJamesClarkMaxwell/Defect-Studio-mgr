# Task 32: drawing control for arrows and orbitals

Not started. Written down 2026-09-18 from user testing of the task 30b branch; no implementation
has been attempted.

## Why

The scene annotations are currently as faithful to the physics as the code can make them, and that
is exactly the problem for a figure. A publication drawing needs the orbital that *reads* clearly at
the size it is printed, and an arrow that can curve around the atom in front of it. Both are
presentation choices the user has to be able to overrule by hand.

## 32a: manual shape control for orbitals

Today an orbital's size and shape fall out of the preset, the shell and the effective charge -
physics in, mesh out. Add explicit per-orbital overrides on top of that, so the drawing can be made
legible without lying about which orbital it is:

- Lobe width and lobe length as independent multipliers, not one uniform `scale`. A p orbital that
  is too fat to fit between two atoms should be squeezable along its own perpendicular without
  becoming a different orbital.
- Per-axis stretch in the orbital's own frame, so a lobe can be pushed toward or away from its
  centre.
- Whatever else the isosurface exposes cheaply: isovalue is already the physical knob that changes
  apparent size, so decide whether it belongs in the same UI group or stays separate. Isovalue
  changes what the surface *means*; the multipliers do not. They should not look like the same kind
  of control.

Open questions to settle before building:

- Does an override belong on the orbital object (persisted, per instance) or on a shared style
  preset? The user has asked for "manual" - start per instance.
- Does a stretched orbital keep claiming its preset name in the outliner, or does it get a marker
  that it has been shaped by hand? A figure caption depends on the answer.
- The two-centre presets (sigma/pi/delta) have two lobes on two atoms; a width multiplier has to
  apply symmetrically or the drawing stops being a bond.

## 32b: curved arrows, paths, and arrow tips

- **Curved arrows.** The classic chemistry arrow - a curve from A to B rather than a straight
  segment. The plan for `task/2x` already carries editable Bezier curves as a scene object; decide
  whether a curved arrow is that curve with a head, or a separate arrow kind with a curvature
  parameter. The cheap version is one control point and a fixed head at the end.
- **Paths.** A multi-point polyline/curve the user can keep extending, for reaction coordinates and
  diffusion hops.
- **Arrow tips.** Today `ArrowKind` is Line / Arrow2D / Arrow3D and the head is one fixed cone or
  one fixed 2D triangle. Needed: a tip *style* chosen independently of 2D-vs-3D - plain, barbed,
  open/half, bar, circle, no head - and per-end, since a double-headed arrow and a bar-at-one-end
  arrow are both normal in a figure. TikZ's arrow tip vocabulary is the reference the user has in
  mind; it does not need to be exhaustive, it needs to not be one cone.

## Decided 2026-09-18

**32b is one object, not three.** `SceneArrow` gets a list of points instead of `start`/`end`, plus
a curvature/control point and a tip style per end. A plain arrow is two points with a tip on one
end; a curved arrow is two points and a control point; a path is N points with both tips set to
none. One geometry builder, one YAML shape, one outliner row kind, one properties section.
`ArrowKind` (Line / Arrow2D / Arrow3D) survives only as *how it is drawn*, not as *what it is*.

Checked before deciding: there is no Bezier or curve scene object in `src/` - `grep -rli
"bezier\|SceneCurve" src/` returns nothing. The editable-curve item in the scene-tools plan was
never built, so 32b is not duplicating it, it *is* it. Close that plan item when this lands.

**Order.** Task 30c (deletion resync + atom buffer) ships and task 30 merges first. Task 32 starts
on a clean tree, because 32 adds new scene objects and the 30c deletion fix is the thing that makes
new scene objects delete correctly for free.

Still open: whether 32a and 32b are one branch or two. 32b is the bigger half.
