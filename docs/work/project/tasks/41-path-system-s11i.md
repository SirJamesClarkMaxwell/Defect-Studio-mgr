# Task 41 S11i: the manim/TikZ tip vocabulary, and tips that are the shape they are named after

## Goal

Endpoint decorations are wrong in three ways the S11 manual round caught, and thin in a fourth:

1. `Square` tapers to a point, so it is a triangle with a different name.
2. `Circle` is a five-point lens, so it reads as a leaf, not a disc.
3. `OpenArrow` carries `filled = false` and the mesher renders it solid anyway.
4. A tip's proportions (`lengthScale`, `widthScale`) exist in the model and in the file format but
   are not editable anywhere in the UI, so every tip in the running app is stuck at 1.0 / 1.0.

After this task the decoration vocabulary is the manim/TikZ one, every kind is the shape it is
named after, `filled` works for every kind rather than for none, and the Properties panel can edit
kind, both scales and `filled` for each endpoint.

## The design, already decided

`PathDecorationKind` in `src/Renderer/Path/PathStyle.hpp` is already rewritten and is the contract:

    None, Arrow, Stealth, Latex, Bar, Circle, Square, Diamond, Kite

`OpenArrow` is **removed as an enumerator**. It was Arrow with `filled = false` spelled as a third
thing. `PathEndpointDecoration::filled` (also already added) now applies to every kind, which gives
the filled/hollow cross product manim spells as separate classes (`ArrowCircleTip` vs
`ArrowCircleFilledTip`) without doubling the enum or the persisted names.

The exact per-kind contour geometry - what Square, Circle, Latex and Kite must be - is written out
in the contract comment above `BuildDecorationContour` in `src/Renderer/Path/PathDecoration.hpp`.
Implement that comment. Do not invent different proportions.

## Files to create or change

- `src/Renderer/Path/PathDecoration.cpp` - the contour table, all nine kinds
- `src/Renderer/Path/PathStrokeMesher.cpp` - honour `DecorationContour::filled`; a hollow contour is
  an outline of stroke-width thickness, not a solid body. This is defect 3 and it is a mesher bug,
  not a table bug.
- `src/Renderer/Scene/ScenePathPersistence.cpp` - the name tables at lines ~75, ~256 and ~301, the
  `filled` round trip, and the `OpenArrow` -> `{ Arrow, filled = false }` migration
- `src/IO/SceneObjectsYaml.cpp` - the accepted-name list at line ~275, and reading/writing the two
  new `filled` keys
- `src/Presentation/Panels/ScenePathEditorWidget.cpp` - the endpoint editor: kind combo, both
  scales, `filled` checkbox, per endpoint
- `src/Presentation/Panels/ScenePathDevMenu.cpp` - only if it names a removed enumerator

## Files that must NOT be touched

- `src/Renderer/Path/PathStyle.hpp`, `src/Renderer/Path/PathDecoration.hpp`,
  `src/Presentation/Panels/ScenePathEditorWidget.hpp`, `src/IO/SceneObjectsIO.hpp` - all four are
  written and are the contract
- `src/Renderer/Path/PathTessellator.cpp`, `PathFrames.cpp` - frames are S11h's territory, and a
  change there while S11h is open makes both impossible to review
- `src/Renderer/OpenGl/` - no shader or renderer change is needed for this
- anything under `tests/` - a separate session owns the tests
- `src/Renderer/Arrow/`, `SceneArrowGeometry` - the legacy arrow keeps its own table until S16

## Acceptance criteria

1. `Square`'s contour has constant `halfWidth` from `s == 0` to `s == length`. It does not taper.
2. `Circle`'s contour has at least 12 points, and every point lies on the half-circle of radius
   `length / 2` centred at `s == length / 2`, to within a small tolerance.
3. `Diamond` is widest at `s == length / 2`; `Kite` is widest at `s == length / 3`; both return to
   `halfWidth == 0` at `s == length`.
4. `Latex` closes back and has strictly more than three points, so it is a curve and not a triangle
   under another name.
5. `Bar` extends about one stroke width along `s` and does not taper.
6. `Stealth` still does not close back, and `Arrow` still does.
7. `BuildDecorationContour` copies `decoration.filled` into `DecorationContour::filled` for every
   kind - including `None`, whose empty contour is unaffected either way.
8. The mesher emits a hollow body when `filled == false`: for the same kind and scales, the unfilled
   mesh has a hole, i.e. its vertex count differs from the filled one and its vertices are not a
   subset of a solid fan. State in the report which observable property the tests should assert.
9. Every existing invariant of the contract comment still holds: `None`, a non-finite scale, a
   non-positive scale or a non-positive width yields an empty contour with `trim == 0`; points are
   ordered by non-decreasing `s` starting at the tip; every value is finite.
10. A saved file naming `OpenArrow` loads as `{ kind = Arrow, filled = false }`.
11. `OpenArrow` is never written back out. A file that contained it and is re-saved names `Arrow`
    and carries `start_decoration_filled: false` (or the end equivalent).
12. A v2 file with no `*_decoration_filled` key loads with `filled == true`, and the format version
    stays 2. This is an additive change, not a version bump.
13. Every new kind round-trips through save and load by name.
14. The legacy v1 `Open` tip name still maps to `{ Arrow, filled = false }`
    (`ScenePathPersistence.cpp:301`).
15. `ScenePathStyleEdit` carries a whole `PathEndpointDecoration` per endpoint, and
    `ResolveScenePathStyleEdit` sets `mixedStartDecoration` / `mixedEndDecoration` when ANY field of
    that endpoint differs across the selection - kind, either scale, or `filled`.
16. `ApplyScenePathStyleEdit` writes all four fields of each endpoint decoration.
17. The Properties panel draws, per endpoint: the kind combo with all nine names, a scale control
    for length, one for width, and a `filled` checkbox.
18. The scale controls obey S11g: dragging one is a single undo entry, not one per frame. S11g lands
    first; use `BeginScenePathStyleDrag` / `CommitScenePathStyleDrag` the same way the Width slider
    does.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` is the exception-free zone - no `throw` in
  `PathDecoration.cpp` or `PathStrokeMesher.cpp`; reject bad input by returning the empty contour
  the contract already specifies.
- `.cpp` files stay under ~500 lines. `PathDecoration.cpp` is 72 today and nine contours will not
  reach that. `PathStrokeMesher.cpp` is 320; if the hollow-body work pushes it past ~450, split the
  decoration meshing into its own file rather than letting it grow.
- Removing the `OpenArrow` enumerator breaks every file that names it. Fixing those is part of this
  task; changing the enum back is not.
- The tree does not compile until this task is done - the headers were written first on purpose.
  That is expected, not something to work around by reverting a header.
- Do not build and do not run tests - the sandbox cannot, and the verifying session does it.
- Do not change any test file or any test expectation.
