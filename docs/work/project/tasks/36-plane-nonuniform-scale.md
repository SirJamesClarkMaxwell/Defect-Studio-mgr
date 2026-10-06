# Task 36: scale a plane's width and height independently

From task 35 item #7: "jak jest skalowanie płaszczyzn to brak tutaj skalowania tylko jednego boku,
tzn, że robimy płaszczyznę szerszą albo wyższą" - today, scaling a `ScenePlane` with the transform
gizmo (or `S`) scales both `halfExtents.x` (tangent axis) and `halfExtents.y` (bitangent axis)
together. The user wants to widen or heighten a plane without changing the other axis.

## Goal

After this task, the transform gizmo's scale handles (and `S` + axis constraint, the same
`Sx`/`Sy`/`Sz` pattern atoms/labels/arrows already use) can scale a selected plane along just its
tangent axis, just its bitangent axis, or both together (uniform, today's behaviour) - the user's
choice, not a hardcoded uniform-only scale.

## Where this lives

- `src/Renderer/Scene/SceneTransform.{hpp,cpp}` - `ApplyTransformDelta` (or wherever plane scale is
  applied) needs to resolve a world-space axis constraint (X/Y/Z, from the modal transform's
  existing constraint state) into the plane's own tangent/bitangent basis, and scale only the
  extent(s) that constraint's axis actually projects onto. Read how atoms/labels already handle an
  axis-constrained scale (`Sx`/`Sy`/`Sz`) before inventing a second mechanism - the constraint
  itself already exists, only the plane needs to consume it correctly for a 2D (not 3D) extent.
- `tests/Renderer/SceneTransformTests.cpp` - `SceneObjectGizmoTests.ScaleGrowsTheOrbitalAndThePlaneExtents`
  already exists and currently only checks *uniform* plane scale keeps working (uniform must stay
  exactly as accurate as it is today - do not regress it). Add axis-constrained cases next to it.

## Files that must NOT be touched

- `src/Renderer/Scene/ScenePlaneGeometry.{hpp,cpp}` - `ScenePlaneCorners`/anchoring/border-width
  logic is unrelated and already covered by its own tests (including the new screen-space border
  width work from tonight - do not touch `ScenePlaneBorderWidth`/`WorldUnitsPerPixelAt`).
- `src/Renderer/RendererWindowState.hpp` - `ScenePlane::halfExtents` is already a `glm::vec2`; no
  new field should be needed, this is about how the existing one gets written.
- Anything under `src/Domain/`, `src/IO/`, `src/App/`, `src/ScientificRuntime/`.

## Acceptance criteria

1. A GoogleTest scales a plane with an X-axis (or whichever axis maps to its tangent) constraint
   active and asserts `halfExtents.x` changed while `halfExtents.y` did not (within float
   tolerance), and vice versa for the other axis.
2. A GoogleTest asserts unconstrained (no axis key held) scale still changes both `halfExtents.x`
   and `halfExtents.y` together, matching the existing `ScaleGrowsTheOrbitalAndThePlaneExtents`
   case exactly - this is a regression guard, not new behaviour.
3. `normal`/`tangent` stay unit length and perpendicular after any of the above - the renderer and
   `PickScenePlane` both assume an orthonormal frame (existing project constraint, unchanged).
4. Full Release test suite green: 2 skipped is the `DS_PYTHON_CAPI_AVAILABLE=0` count, not a
   regression.

## Constraints

- Layer boundaries from `AGENTS.md` are hard. `Renderer/Scene/SceneTransform.cpp` is renderer-side
  geometry with no ImGui in it.
- No exceptions in rendering paths.
- `.cpp` files stay under ~500 lines.
- Do not build or run anything needing an approval step. Report what you changed; the dispatching
  session builds, runs the suite and exercises the app.
