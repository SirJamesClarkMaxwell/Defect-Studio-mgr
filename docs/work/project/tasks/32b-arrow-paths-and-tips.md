# Task 32b: an arrow that is a path, with a tip vocabulary

Branch: `task/32b-arrow-paths-and-tips`, on top of `task/30d-arrow-atom-anchoring`. Do not start
before 30d is merged - it rewrites the same fields.

Decided in `32-drawing-control-arrows-and-orbitals.md`: one point-list object, not three. A plain
arrow is two points with a tip on one end, a curved arrow is two points with a control point, a path
is N points with both tips set to none.

## Why this is split into 32b-1 and 32b-2

The two arrow renderers are not the same kind of thing, and one task cannot honestly cover both:

- `Arrow3D` is real geometry - a shaft mesh plus a cone, composed into one mesh
  (`OpenGlRendererBackend.cpp:599`, cone at `:1559`). Generalising a one-segment shaft into a tube
  along N points is ordinary mesh work, and a tip is another mesh.
- `Arrow2D` is a **single quad with an SDF fragment shader** - a shaft-rectangle and a head-triangle
  unioned analytically (`OpenGlRendererBackend.cpp:971`, quad at `:1686`, its own depth-disabled
  pass at `:1265`). A closed-form SDF for a rectangle-plus-triangle does not generalise to an
  arbitrary polyline or a Bezier, and the honest fix for 2D is to stop drawing it as one quad and
  draw a camera-facing ribbon instead. That is a rewrite of a renderer that currently works.

So: **32b-1 is the data model, the curve, and the 3D/Line path. 32b-2 is 2D parity.** Splitting it
this way keeps a working `Arrow2D` on screen the whole time instead of leaving the user with a
half-converted one.

---

# 32b-1: point list, curvature, and the tip vocabulary

## Goal

A scene arrow carries an ordered list of points instead of one `start`/`end` pair, optionally
curved, with a tip style chosen per end independently of whether it is drawn 2D or 3D. `Line` and
`Arrow3D` draw all of it. `Arrow2D` keeps behaving exactly as it does today for the two-point,
uncurved case and is left for 32b-2.

## Design

On `RendererWindowState::SceneArrow`, replacing `start`/`end`:

```cpp
// Ordered path points, world space. Always at least 2. Two points with no control point is the
// straight arrow every existing project file contains; more points make a path. `start` and `end`
// become accessors onto points.front()/points.back() so the many existing readers keep compiling -
// see the migration note below.
std::vector<glm::vec3> points;
// Optional single quadratic control point between the first two entries. One control point, not a
// full Bezier chain: it is the cheap version the plan calls for and it covers the chemistry arrow.
// Ignored when `points.size() > 2` - a path bends by having more points.
std::optional<glm::vec3> controlPoint;
// How many line segments a curved span is tessellated into when it is meshed.
int curveSegments = 24;
ArrowTip startTip = ArrowTip::None;
ArrowTip endTip = ArrowTip::Plain;
```

and beside `ArrowKind`:

```cpp
// Chosen independently of ArrowKind and independently per end - a double-headed arrow and a
// bar-at-one-end arrow are both ordinary in a figure. TikZ's vocabulary is the reference; this is
// deliberately a small subset of it, not an attempt to be exhaustive.
enum class ArrowTip { None, Plain, Barbed, Open, Bar, Circle };
```

**The vocabulary is defined once, as data.** Add one function that maps an `ArrowTip` to the
parameters a renderer needs (length along the shaft, width across it, whether it is filled or a
pair of strokes, whether it closes at the back). Both the 3D mesh builder and, in 32b-2, the 2D
path read that one table. If `Arrow2D` grows its own private notion of what `Barbed` means, the two
drawings will drift apart and the feature is worse than not having it.

**Migration of `start`/`end`.** They are read in the renderer, picking, the gizmo, `SceneTransform`,
the outliner, the properties panel and IO. Do not chase every reader. Keep `start()`/`end()` as
inline accessors returning `points.front()`/`points.back()` (and non-const versions for the writers)
so existing call sites keep working, and only touch a reader when it genuinely needs the middle of
the path. State in your report which readers you did have to change and why.

**Anchoring, from 30d.** `startAnchorAtom`/`endAnchorAtom` anchor the first and last point. Interior
points are never anchored in this task. The per-frame refresh from 30d writes
`points.front()`/`points.back()`; it must not resize the vector.

**Persistence.** `points` as a sequence of vec3 in YAML, `control_point` optional, `curve_segments`,
`start_tip`/`end_tip` as strings (not ints - an enum written as `2` is unreadable and breaks the
moment the enum is reordered). **A project file written before this change has `start` and `end`
keys and no `points`**: load them into a two-element `points`, defaulting `endTip` to `Plain` for
`Arrow2D`/`Arrow3D` and `None` for `Line`, so every existing arrow reloads looking exactly as it
does now. This is the acceptance criterion most likely to be got wrong; write it first.

**Rendering, 3D and Line.** Generalise the existing shaft composition to walk the point list,
tessellating a curved span through `controlPoint` into `curveSegments` straight pieces. Reuse the
existing dash parameterisation - dashes measure along the accumulated path length, not per segment,
or a dashed curve will visibly restart its pattern at every joint. Tips are meshes placed at the
first and last point, oriented along the first and last segment.

## Out of scope for 32b-1

- `Arrow2D` beyond what it does today. If `points.size() > 2` or `controlPoint` is set, draw the
  straight two-point form it draws now and leave it visually unchanged. Do not half-convert it.
- Editing interior points in the viewport. Adding and dragging path points is its own task; here the
  list is created straight (a path can still be built from the properties panel).
- Interior anchoring, variable width along the path, arrow labels.

## Acceptance criteria

1. A GoogleTest loads a pre-32b project file (literal YAML in the test, `start`/`end`, no `points`)
   and asserts the arrow comes back with two points equal to the old values and the tips defaulted
   per kind.
2. A GoogleTest round-trips a 4-point path with a control point and both tips set, and asserts every
   field including the tip *strings*.
3. A GoogleTest asserts the tip parameter table returns distinct geometry parameters for each
   `ArrowTip` value and that `None` produces no tip geometry.
4. A GoogleTest meshes a curved two-point arrow and asserts the mesh follows the control point -
   the midpoint of the built path is displaced toward it - and that raising `curveSegments`
   increases vertex count without moving the endpoints.
5. A GoogleTest asserts a dashed path's dash pattern is continuous across a joint: the length of the
   first dash after a joint equals what the accumulated-length parameterisation predicts, not a
   fresh dash.
6. `scripts/Windows/Build.bat --config Release` builds both targets with zero errors and zero
   warnings. 2 skipped tests expected (`DS_PYTHON_CAPI_AVAILABLE=0`).

## Constraints

- Layer boundaries from `AGENTS.md` are hard. The tip table is Renderer data, not Domain. `IO` only
  round-trips fields and must not learn what a tip looks like.
- Only the main thread mutates state visible in the project or UI.
- `.cpp` files stay under ~500 lines. `OpenGlRendererBackend.cpp` is already very large - the path
  and tip mesh building goes in its own file, not appended to it.
- Do not add a second arrow renderer beside the existing one. Generalise the path that is there.
- Do not build or run anything needing an approval step. Report what you changed; the dispatching
  session builds, runs the suite and exercises the app.

---

# 32b-2: Arrow2D parity (not started, no contract yet)

`Arrow2D` stops being one SDF quad and becomes a camera-facing ribbon built from the same point
list, with tips from the same table. Write this contract only after 32b-1 has shipped and the tip
table has proven itself against real geometry - its shape is what 32b-2 consumes, and guessing at it
now would mean writing the contract twice.
