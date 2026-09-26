# Task 41 S11j: tips that look like tips, and a scene that shows them all

## Goal

The manual round produced a photo of nine decorated paths. Three things are wrong with it, and one
thing is missing.

1. **Every arrowhead is as wide as the shaft.** `PathDecoration.cpp` computes
   `arrowWidth = width * 0.5` and hands it to Arrow, Stealth, Latex, Diamond and Kite, while Square
   and Bar use the full `width`. At the default `widthScale = 1`, Arrow's half-width came out at
   `0.5 * strokeWidth`, which is exactly the tube's radius - so an arrowhead has no flare at all and
   reads as a sharpened pencil.
2. **Bar and Square are the same shape.** Bar's contour spans the full `length` at constant
   half-width, which revolves into a barrel; so does Square. They differ only in how long the
   barrel is.
3. **Latex is a symmetric lens.** Its contour is `sin(pi * f)`, which returns to half-width 0 at
   the back. TikZ's `latex` has a flat, swept back.
4. **There is no way to get that photo back.** The user rebuilt nine paths by hand to take it.

## The contract, already corrected

Read `src/Renderer/Path/PathStyle.hpp` and `src/Renderer/Path/PathDecoration.hpp`; both are already
updated and are the contract.

- `PathEndpointDecoration` now defaults to `lengthScale = 3.0f`, `widthScale = 1.25f`.
- `width` means the HALF-width, for every kind. The internal `arrowWidth = width * 0.5` goes away.
  Do not reintroduce it, and do not compensate for its removal by halving something else.
- Bar's axial extent is `0.3 * lengthScale * strokeWidth`, not `length`.
- Latex's contour ends at `(length, width * 0.35)` with a flat back rather than returning to zero.

## The dev scene

Add a dev-menu entry that builds the whole gallery in one click: one path per decoration kind,
laid out so they do not overlap, each carrying that kind as its START decoration and a plain Arrow
at the end, so a comparison shot is one action rather than nine.

`src/Presentation/Panels/ScenePathDevMenu.{hpp,cpp}` already owns the dev presets
(`MakeDevScenePath`, `ScenePathDevPreset`) and the menu that inserts them. Extend it. Keep the
existing presets working.

Requirements for the gallery:

- One path per kind in `PathDecorationKind` except `None`, in enum order, so the row order is
  predictable and a screenshot can be read against the enum.
- Straight lines, parallel, offset along one axis by enough that neighbouring tips do not touch.
- Each path named after its kind, so the Outliner and the Properties header say which is which.
- It inserts through the same command path as the existing dev presets, so the whole gallery is
  one undo entry rather than nine.

## Files to create or change

- `src/Renderer/Path/PathDecoration.cpp` - the four corrections above
- `src/Presentation/Panels/ScenePathDevMenu.cpp` - the gallery
- `src/Presentation/Panels/ScenePathDevMenu.hpp` - ONLY if the gallery needs a new declaration;
  keep it to a single added enumerator or function if so

## Files that must NOT be touched

- `src/Renderer/Path/PathStyle.hpp`, `src/Renderer/Path/PathDecoration.hpp` - the contract
- `src/Renderer/Path/PathStrokeMesher.cpp`, `PathDecorationMesher.cpp` - the meshing is correct;
  this is a contour-table change
- `src/Renderer/Scene/ScenePathPersistence.cpp`, `src/IO/` - no format change here
- anything under `tests/` - a separate session owns the tests

## Acceptance criteria

1. No `arrowWidth` or any other per-kind halving remains in `PathDecoration.cpp`. Every kind that
   has a widest point reaches exactly `widthScale * strokeWidth` there.
2. With default scales and `strokeWidth == 0.05`, an Arrow's back half-width is strictly greater
   than the tube's radius (`strokeWidth / 2`). This is the defect: assert the inequality.
3. Bar's axial extent is `0.3 * lengthScale * strokeWidth`, and is strictly less than Square's at
   the same scales.
4. Latex's last contour point has a half-width strictly greater than zero and strictly less than
   its widest point.
5. Every other per-kind property already asserted by `PathDecorationTests` still holds: Square's
   constant half-width, Circle's points on its half-circle, Diamond at 1/2 and Kite at 1/3, Arrow
   closing back and Stealth not, `filled` copied for every kind, and the empty-contour rules for
   `None` and for non-finite or non-positive scales.
6. The dev menu offers the gallery, and it creates one path per kind except `None`.
7. The gallery's paths do not overlap, and each is named after its kind.
8. The whole gallery is one undo entry.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer` stays exception-free.
- `.cpp` files stay under ~500 lines.
- Do not build, do not run tests, do not commit. The verifying session does all three.
- `FlatAndRoundDecorationVerticesRemainStable` in `tests/Renderer/Path/PathStrokeMesherTests.cpp`
  WILL fail after this change, because it pins vertex positions recorded under the old `width * 0.5`
  proportion. That is expected and it is not yours to fix - the verifying session re-records it
  from a real run. Do not touch it, and do not try to preserve the old proportion to keep it green.
