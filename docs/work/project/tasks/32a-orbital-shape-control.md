# Task 32a: manual shape control for orbitals

Branch: `task/32a-orbital-shape-control`. See `docs/work/project/tasks/32-drawing-control-arrows-and-orbitals.md`
for why this exists. Short version: an orbital drawn faithfully to the physics is often the wrong
picture for a figure, and the user needs to squeeze one by hand without it stopping being that
orbital.

## Goal

An orbital's lobes can be stretched independently along the orbital's own axes - narrower to fit
between two atoms, longer to reach across a bond - while `effectiveCharge` and `isoFraction`, the
two knobs that change what the surface physically *means*, stay exactly where they are.

## Design

`SceneOrbital` already carries `float scale` - a uniform mesh scale about the centroid, applied at
`SceneOrbitalGeometry.cpp:163`:

```cpp
vertex.position = centroid + (vertex.position + centeringOffset - centroid) * orbital.scale;
```

Add one field beside it:

```cpp
// Per-axis stretch in the orbital's OWN frame, on top of the uniform `scale` - the figure knob
// for "this lobe is too fat to fit between those atoms". x and y are the two directions across a
// lobe, z is along it. Like `scale` it changes only the drawing: effectiveCharge contracts the
// wavefunction, isoFraction picks a different surface of it, this moves vertices. A drawing that
// has been stretched is still the same orbital, which is why it keeps its preset name.
glm::vec3 stretch = glm::vec3(1.0f);
```

**Which frame.** For a single-centre preset, the orbital's frame is the one `rotationEuler` already
builds (`SceneOrbitalGeometry.cpp:129-130`). For a two-centre preset the frame comes from
`centerB - centerA` - that axis is z, and any two perpendicular unit vectors complete it. Apply the
stretch in that frame: rotate the vertex into it, multiply per axis, rotate back, then apply the
existing uniform `scale` as it is applied today. The two-centre case is then symmetric for free,
because the scaling is about the centroid in the bond's own frame - which is what the plan file
requires ("a width multiplier has to apply symmetrically or the drawing stops being a bond").

**Do not** turn `float scale` into a `glm::vec3`. It is persisted, it is in the mesh key, it is in
tests, and it means something the new field does not: one honest "make the whole thing bigger"
number. Keep both.

Three places that must learn about the new field, all of them within a few lines of where `scale`
is already handled:

- `SceneOrbitalGeometry.cpp:163` - the vertex transform.
- `SceneOrbitalGeometry.cpp:178-179` - `HashVec3(hash, orbital.stretch);` into `SceneOrbitalMeshKey`,
  otherwise the mesh cache will not re-bake when the user drags the slider and nothing will appear
  to happen. This is the one that will bite if it is missed.
- `SceneOrbitalGeometry.cpp:248-249` - the bounds radius. Use the largest stretch component so
  picking and camera framing do not clip a stretched orbital.

Also guard it the way `scale` is guarded at line 143: a non-finite or non-positive component falls
back to `1.0f` rather than producing a degenerate mesh.

**UI** (`ObjectPropertiesPanelOrbital.cpp`, the orbital section): two controls, not three sliders.

- `Szerokosc` drives `stretch.x` and `stretch.y` together - that is the control the user actually
  asked for, and a lobe that is fatter in x than in y is almost never what someone wants.
- `Dlugosc` drives `stretch.z`.
- Behind a collapsed `Zaawansowane` header, the three components individually, for the rare case.
- Put them in their own group, visually separated from `Iso` and `Ladunek efektywny`, with a one-line
  caption saying these change the drawing and not the physics. The plan file is explicit that these
  two kinds of control must not look like the same kind of control.
- Range 0.1 to 5.0, step small enough to drag smoothly. Use `DrawUndoableValue`, the same wrapper
  every other orbital control in that file uses, so the change lands on the undo stack.

**Persistence.** Round-trip as `stretch` beside the existing `scale` key in
`SceneObjectsYaml.cpp` and `SceneObjectPersistence.cpp`, with the same shape as the other `glm::vec3`
fields in that file. An older project file without the key loads with `stretch == vec3(1.0f)`.

## Files to create or change

- `src/Renderer/RendererWindowState.hpp` - the field.
- `src/Renderer/Scene/SceneOrbitalGeometry.cpp` / `.hpp` - transform, mesh key, bounds, guard.
- `src/Presentation/Panels/ObjectPropertiesPanelOrbital.cpp` - the controls.
- `src/IO/SceneObjectsYaml.cpp`, `src/IO/SceneObjectsIO.hpp`,
  `src/Renderer/Scene/SceneObjectPersistence.cpp` - round-trip.
- `tests/` - see Acceptance criteria.

## Files that must NOT be touched

- `src/Domain/` - `HydrogenicOrbital` and the preset tables stay untouched. This task does not
  change the physics, and a change there would mean it does.
- `src/App/`, `premake5.lua`.
- `src/Presentation/Panels/SceneArrow*`, `src/Renderer/Scene/SceneTransform.cpp`,
  `ViewportSceneArrowInteraction.cpp` - a separate task is editing arrows in parallel. Stay out of
  the arrow files entirely.
- The uniform `scale` field's meaning, its YAML key, and its existing tests.

## Acceptance criteria

1. A GoogleTest meshes an orbital twice, once with `stretch == vec3(1.0f)` and once with
   `stretch.z == 2.0f`, and asserts the second mesh's extent along the orbital's own z grew while
   its extent across x and y did not.
2. A GoogleTest asserts two `SceneOrbitalMeshKey`s differ when only `stretch` differs. Without this
   the cache serves a stale mesh and the control looks dead.
3. A GoogleTest covers a two-centre preset: stretching width leaves the two lobes symmetric about
   the bond axis, and the centroid does not move.
4. A GoogleTest asserts a non-finite or zero `stretch` component is treated as `1.0f` and the mesh
   is still produced.
5. A GoogleTest round-trips `stretch` through the project file, and a file written without the key
   loads as `vec3(1.0f)`.
6. `scripts/Windows/Build.bat --config Release` builds both targets with zero errors and zero
   warnings. 2 skipped tests expected (`DS_PYTHON_CAPI_AVAILABLE=0`).

## Constraints

- Layer boundaries from `AGENTS.md` are hard. This is all Renderer and Presentation; `Domain` does
  not learn that a drawing can be stretched.
- Only the main thread mutates state visible in the project or UI.
- Meshing already runs on the main thread when a parameter changes - do not add a second bake path
  and do not move it to a thread. If dragging the new slider hitches at high `resolution`, say so in
  your report; the fix is JobSystem and it is out of scope here.
- `.cpp` files stay under ~500 lines. Extract rather than append.
- Do not build or run anything needing an approval step. Report what you changed; the dispatching
  session builds, runs the suite and exercises the app.
