# Task 84: hide volumes - hide atoms with a sphere, box or cylinder

Branch `task/84-hide-volumes` off `task/70-operator-redo-panel` (HEAD `777f590`).

A hide volume is a scene object that hides every atom inside it (or outside it, inverted). It is
the per-material render preset: one setup for diamond, another for hBN, copied between structures,
so the view for a defect or a wavefunction render is two clicks instead of a manual H sweep.

## Decisions already taken (do not re-open)

- **Three shapes in the first iteration:** sphere, box (OBB), cylinder.
- **Two frames, switchable per volume:** `Anchored` (centre follows atoms, dimensions in Angstrom)
  and `Fractional` (centre and dimensions as fractions of the lattice vectors). A defect preset is
  Anchored - "6 A around the vacancy" means the same thing in a 2x2x2 and an 8x8x8 cell. Fractional
  is for "show the middle layer", where growing with the cell is the point.
- **Lives in `scene_objects.yaml`** as `kind: SceneHideVolume`, keyed per structure like every other
  scene object. Sharing between cells is copy/paste, not a separate preset library.

## What already exists - reuse it, do not rewrite it

- `RendererWindowState::ScenePlane` (`RendererWindowState.hpp:336`) - the closest precedent for a
  scene object with a centre, a frame, half-extents and `anchorAtoms`. `SceneHideVolume` is shaped
  like it.
- `PersistedScenePlane` (`IO/SceneObjectsIO.hpp:69`) + `ParsePlane`/`EmitPlane`
  (`IO/SceneObjectsYaml.cpp:210,240`) - the parse/emit pair to copy for the new kind.
- `Renderer/Scene/SceneObjectPersistence.cpp` - maps persisted structs to/from window state.
- `VisibilityComponent{visible, renderable}` (`Scene/SceneComponents.hpp:45`), owned by the ECS
  mirror and pushed by `SceneSystem::PushSelectionAndVisibilityToWindowState`.
- `HideSelectionModifier` / `ShowAllModifier` (`Scene/ViewModifier.hpp:21`), bound to H/Alt+H at
  `RendererLayer.cpp:2438`.
- `RendererStructureData::lattice` (`RendererTypes.hpp:119`), a `glm::mat3` - the Fractional frame
  needs nothing from Domain.
- `ViewportAddMenu.cpp`, `SceneOutlinerRender.cpp`, `ViewportVerticalToolbar.cpp` - the three entry
  points a new scene-object kind has to appear in.

Nothing in the tree does a 3D point-in-volume test. `ViewportSelection.hpp:48` box/circle select is
screen-space only and is not the thing to extend.

## The one real conflict: two sources of hiding

H writes `VisibilityComponent.visible` directly. A hide volume wants to write the same flag, and
would clobber a manual H the moment its radius changes. Resolve it this way and document it in the
header:

- A volume does **not** own `VisibilityComponent`. It contributes a mask, and the effective
  visibility is `manualVisible AND NOT coveredByAnyVolume` (with `invert` already folded into
  `coveredBy`).
- Keep the manual half where it is (`HiddenSceneState`, H, the outliner eye). Add the volume mask
  beside it and combine on push.
- Recompute the mask when a volume is added, edited, deleted or toggled, and when the structure is
  rebuilt - not per frame. Atom positions only move on a rebuild.
- `Alt+H` (show all) clears the manual half only. Showing everything while a volume is active is the
  volume's own eye in the outliner, not Alt+H. Say so in the header comment.

## Do

1. **Domain-free geometry first** - `src/Renderer/Scene/SceneHideVolume.{hpp,cpp}`:
   - `enum class HideVolumeKind { Sphere, Box, Cylinder };`
   - `enum class HideVolumeFrame { Anchored, Fractional };`
   - `struct SceneHideVolume { SceneObjectId id; HideVolumeKind kind; HideVolumeFrame frame;
     glm::vec3 center; glm::vec3 halfExtents; glm::mat3 orientation; float radius; float height;
     glm::vec3 axis; std::vector<std::size_t> anchorAtoms; bool invert; bool visible; bool
     renderable; glm::vec3 color; float alpha; }`
   - `[[nodiscard]] bool PointInHideVolume(const SceneHideVolume &, glm::vec3 worldPoint, const
     glm::mat3 &lattice);` - resolves the frame, then the shape test. Sphere: squared distance.
     Box: transform into the OBB frame, compare per axis. Cylinder: project onto the axis, test the
     axial span and the radial distance.
   - `[[nodiscard]] std::vector<std::size_t> AtomsCoveredByVolumes(const RendererStructureData &,
     std::span<const SceneHideVolume>);`
   - The Fractional path converts the volume's centre and dimensions through `lattice` once per
     evaluation, not per atom.
2. **Tests before the rest** (`tests/Renderer/Scene/SceneHideVolumeTests.cpp`): per shape, a point
   just inside and just outside each face/surface; `invert` flips both; an Anchored volume whose
   anchor atom moved follows it; the same Anchored volume evaluated against a 2x2x2 and an 8x8x8
   lattice covers the same physical radius, while the same Fractional volume scales with the cell -
   that pair is the whole point of the feature, so assert it explicitly.
3. **Mask plumbing:** evaluate `AtomsCoveredByVolumes` where the hidden set is applied
   (`SceneSystem.cpp:177`), AND-ed with the manual set as above. A bond is hidden when either
   endpoint is - the existing H semantics, no new rule.
4. **Persistence:** `PersistedSceneHideVolume` in `IO/SceneObjectsIO.hpp`, `ParseHideVolume` /
   `EmitHideVolume` in `IO/SceneObjectsYaml.cpp`, mapping in `SceneObjectPersistence.cpp`. Bump
   `formatVersion` only if an old file would mis-load; a new kind alone does not need it. Round-trip
   test per shape and per frame.
5. **UI:** Add menu entry, outliner row with both eyes, Object Properties showing kind, frame
   switch, invert, and the parameters of the active kind only. Default a new volume to a sphere
   centred on the selection, Anchored, radius covering it.
6. **Viewport:** draw the volume as a translucent wireframe when selected or when its eye is on, so
   the user can see what is being cut. Reuse the plane's translucent pass; do not add a new one.
   Cylinder needs an axis handle - the other two re-use the existing transform gizmo.
7. **Copy/paste:** outliner context menu Copy / Paste over an in-memory clipboard holding
   `PersistedSceneHideVolume`. Paste into another structure re-resolves `anchorAtoms` by index and
   marks the volume link-broken when the index is out of range, the way pins already do.
   `ponytail:` in-session only, no OS clipboard - emit the YAML text to the OS clipboard when
   someone needs it across projects.

## Report

Write `docs/work/project/tasks/84-hide-volumes-report.md`: every file changed, the precedence rule
as implemented, what the manual-H interaction does in practice, and anything in step 6 or 7 cut
short.
