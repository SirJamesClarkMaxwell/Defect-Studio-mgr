# Task 26c: orbitals on screen

## Goal

A user picks an orbital from the Add menu - `s`, `p`, `d`, an `sp` / `sp2` / `sp3` hybrid lobe, or
a `sigma` / `pi` / `delta` molecular orbital with its antibonding partner - and sees it rendered in
the viewport with its two phases coloured, anchored to the selected atoms when there are the right
number of them. The maths already exists and is tested (task 26b,
`src/Domain/Electronic/HydrogenicOrbital.hpp`); this task is the scene object and the render pass
that put it on screen.

Editing an orbital in the properties panel, saving it in a project, and selecting/transforming it
with the gizmo are task 26d and are explicitly NOT in this one. An orbital added here is drawn with
whatever the Add menu gave it. Keep that in mind when deciding where to put things: 26d will need
to reach the same object, so do not hide anything behind a local.

## Files to create or change

- `src/Renderer/Scene/SceneOrbitalGeometry.cpp` - replace the stub. This is the bulk of the task:
  building the `OrbitalWavefunction` from a `SceneOrbital`, resolving anchors, CPU-meshing it
  through the existing `GenerateIsosurfaceMesh`, applying `scale` about the centroid, hashing the
  shape parameters for the cache, the per-frame anchor resolve, and the defaults.
- `src/Renderer/OpenGl/OpenGlRendererBackend.{hpp,cpp}` - a per-orbital static mesh cache and a
  draw pass. See "How orbitals render" below; this is the part where a wrong choice is expensive.
- `src/Presentation/Panels/RendererPanelToolbar.cpp` - an "Orbital" submenu in `drawAddMenu`
  listing every preset by its `OrbitalPresetName`, each calling `MakeDefaultSceneOrbital` and
  allocating an id from `candidate.sceneRegistry`, exactly as the existing "Arrow" entry does,
  including the `PushPinnedMeasurementUndoSnapshot` call.
- `src/Presentation/Panels/RendererPanel.cpp` - the same submenu in the right-click viewport "Add"
  submenu.
- `src/Presentation/Panels/ViewportInteraction.cpp` - call `ResolveAnchoredOrbitals(windowState)`
  next to the existing `ResolveAnchoredBonds` call.
- `src/Presentation/Panels/SceneOutlinerPanel.cpp` - list orbitals as their preset name, e.g.
  "pi* #0". Extend the existing per-kind naming; do not add a second listing path.
- `src/Renderer/RendererLayer.cpp` - wherever the undo snapshot for scene objects is captured and
  restored, `sceneOrbitals` has to travel with it. `LabelUndoSnapshot` already has the field; find
  every place that fills or reads that struct and cover them all. A `grep` for `sceneArrows` in
  that file is the reliable way to find them - every site that copies `sceneArrows` needs the same
  line for `sceneOrbitals`.

## Files that must NOT be touched

- `src/Renderer/Scene/SceneOrbitalGeometry.hpp` and `src/Renderer/RendererWindowState.hpp` - the
  contract. If a signature or a field is wrong, say so; do not change it.
- Everything under `tests/` - also the contract, for the same reason.
- Anything under `src/Domain/` - the maths is done and tested. If an orbital looks wrong, the bug
  is in how this task builds the `OrbitalWavefunction`, not in the evaluator.
- `src/IO/` and `src/Renderer/Scene/SceneObjectPersistence.cpp` - persistence is task 26d.
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp` - the editor is task 26d.
- The existing WAVECAR orbital overlay: `RendererLayer::RegenerateOrbitalIsosurface`,
  `RendererWindowState::OrbitalOverlayChannel`, `isosurface_march.comp` and the compute dispatch in
  the backend. Scene orbitals are a separate, additive path - see below.

## How orbitals render

Reuse the CPU mesher, not the GPU compute path.

`GenerateIsosurfaceMesh` already exists, is tested, and returns exactly the vertex format the
isosurface shader draws. `BuildSceneOrbitalMesh` returns its output. So the backend needs: one
static VBO per orbital, uploaded when the orbital's `SceneOrbitalMeshKey` changes, drawn with the
existing isosurface program with the orbital's own `positiveLobeColor` / `negativeLobeColor` /
`alpha` as uniforms.

`OpenGlSceneArrowMeshCache` in `OpenGlRendererBackend.cpp` is the pattern to copy: a per-object
mesh cache that grows with the vector, drops slots when it shrinks, and releases its GL handles
through `DeleteMeshHandles`. Do the same, keyed by `SceneObjectId` so reordering the vector does
not invalidate every mesh.

Do NOT extend `kIsosurfaceSlotCount` or the per-window compute buffers. Those are two ~64MB vertex
buffers built to be re-dispatched every frame while the user scrubs an iso value on one big WAVECAR
grid. A scene orbital is small and only changes when someone edits it, so it gets baked once and
kept. Growing that array to N orbitals would cost hundreds of megabytes per window for no benefit.

An orbital with `visible == false`, or one whose mesh came back empty, draws nothing and must not
leave the GL state dirty for whatever draws next.

## Acceptance criteria

1. `tests/Renderer/Scene/SceneOrbitalGeometryTests.cpp` passes in full.
2. Every other existing test still passes. Nothing in this task changes an existing test's result.
3. `scripts/Windows/GenerateProjects.bat` has been run - this task adds `SceneOrbitalGeometry.cpp`
   and its test file, and premake globs sources at generation time. (Already run before dispatch;
   re-run only if you add a further new file.)
4. Adding an orbital from either Add menu produces a visible, two-coloured isosurface in the
   viewport, and adding a second one does not disturb the first.
5. Undo after adding an orbital removes it.

## Constraints

- Layer boundaries in `AGENTS.md` are hard. `Renderer` may read domain types - that is what
  `HydrogenicOrbital.hpp` is here for - but domain state is not authored here. `Presentation`
  collects intent; every scene-object mutation from a menu pushes an undo snapshot the way the
  existing "Arrow" entry does.
- No exceptions on a rendering path. A degenerate orbital meshes to nothing; it does not throw.
- `.cpp` files stay under ~500 lines. `OpenGlRendererBackend.cpp` is already very large - put the
  orbital cache and draw pass in a new `src/Renderer/OpenGl/OpenGlOrbitalRenderer.cpp` beside it
  rather than growing that file further, and re-run `GenerateProjects.bat` if you do.
- Meshing runs on the main thread. Only re-bake when `SceneOrbitalMeshKey` changes - never every
  frame. A test pins which parameters belong in that key and which do not.
- Only the main thread commits state visible in the project or UI.
