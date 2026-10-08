#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/RendererTypes.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	// A hide volume hides every atom inside it (or outside it, inverted). It is the per-material
	// render preset: one sphere around the vacancy for diamond, another box for hBN, copied between
	// structures, so setting up a defect or wavefunction render is two clicks instead of a manual H
	// sweep over a few hundred atoms.
	//
	// A volume does NOT own VisibilityComponent. It contributes a mask, and effective visibility is
	// `manualVisible AND NOT coveredByAnyVolume` - otherwise editing a radius would silently clobber
	// whatever the user hid by hand with H. Consequence: Alt+H (show all) clears the manual half
	// only; a volume is switched off by its own eye in the Scene Outliner.
	//
	// Renderer layer: no ImGui, no Presentation includes, no Domain mutation.

	enum class HideVolumeKind
	{
		Sphere,
		Box,
		Cylinder,
	};

	// Which space `center` and `halfExtents` are expressed in. This is what makes a volume a preset
	// that survives a change of cell:
	//
	// - `Anchored`: Angstrom, centre optionally following `anchorAtoms`. "6 A around the vacancy"
	//   covers the same physical ball in a 2x2x2 and an 8x8x8 supercell. This is the defect case.
	// - `Fractional`: fractions of the lattice vectors, so the volume grows with the cell. This is
	//   the "show the middle layer" case, where scaling with the cell is the point.
	//
	// In `Fractional` a sphere is a sphere in fractional space, i.e. an ellipsoid in world space for
	// any non-cubic cell. That is the useful reading of "the middle third of the cell"; a volume that
	// must stay round in world space is an `Anchored` one.
	enum class HideVolumeFrame
	{
		Anchored,
		Fractional,
	};

	struct SceneHideVolume
	{
		// Stable identity allocated by SceneRegistry, as for every other scene object.
		SceneObjectId id;
		// Stable across a save/load, unlike `id` - see RendererWindowState::PinnedMeasurement.
		std::string persistKey;
		HideVolumeKind kind = HideVolumeKind::Sphere;
		HideVolumeFrame frame = HideVolumeFrame::Anchored;

		// Centre, in the space `frame` names. Ignored while `anchorAtoms` resolves.
		glm::vec3 center = glm::vec3(0.0f);
		// Unit, mutually perpendicular columns - the volume's own axes. Identity means axis-aligned.
		// Column 2 is the cylinder's axis; a sphere ignores this entirely.
		glm::mat3 orientation = glm::mat3(1.0f);
		// One field set for all three shapes, so there are no dead members to keep consistent:
		// - Sphere:   x is the radius, y and z are ignored.
		// - Box:      the three half-sides, along the columns of `orientation`.
		// - Cylinder: x is the radius, z is the half-height along the axis, y is ignored.
		glm::vec3 halfExtents = glm::vec3(1.0f);

		// Optional atom anchoring, indices into RendererStructureData::atoms, same lifetime rules as
		// ScenePlane::anchorAtoms: the centre is the centroid of whatever still resolves, and the
		// volume follows the atoms as they move. Empty, or no index in range, means `center` is used
		// as stored. Only meaningful for `Anchored`.
		std::vector<std::size_t> anchorAtoms;

		// Hide what is OUTSIDE instead of inside. The common case for a defect render - keep the
		// cluster around the vacancy, drop the bulk.
		bool invert = false;

		std::string name;
		glm::vec3 color = glm::vec3(0.0f, 1.0f, 1.0f);
		float alpha = 0.75f; // Outline opacity.
		// Whether the volume hides anything at all. Independent of the eyes below: a volume set up
		// once is usually wanted out of sight while it keeps cutting, so hiding its outline must not
		// bring the atoms back (task 91). Turning the cut off is this flag, not the eye.
		bool active = true;
		// The Scene Outliner's two columns, as everywhere else, now about the outline only:
		// `visible` draws the wireframe in the viewport, `renderable` draws it in an exported image.
		// Off by default for the camera - an exported defect render should not carry the cage.
		bool visible = true;
		bool renderable = false;
	};

	// Centroid of the anchor atoms that are in range, or nullopt when the volume is not anchored or
	// nothing resolves - the caller then uses `center` as stored.
	[[nodiscard]] std::optional<glm::vec3> ResolveHideVolumeCenter(
		const SceneHideVolume &volume, const RendererStructureData &structure);

	// True when `worldPoint` is covered by `volume`, `invert` already applied. `lattice` is
	// RendererStructureData::lattice, used only by the `Fractional` frame; a singular lattice makes
	// every fractional test false rather than producing NaNs.
	//
	// The frame transform happens in front of a single shape test: the point is brought into the
	// volume's own space (fractional if asked, then centred, then rotated by the inverse of
	// `orientation`), and the shape is tested there.
	[[nodiscard]] bool PointInHideVolume(
		const SceneHideVolume &volume,
		glm::vec3 worldPoint,
		const glm::mat3 &lattice,
		const RendererStructureData &structure);

	// Indices into `structure.atoms` covered by at least one volume with `active` set, ascending and
	// unique. Inactive volumes contribute nothing; the eyes do not matter. Evaluated when a volume changes or the
	// structure is rebuilt - atom positions only move on a rebuild, so this is not per-frame work.
	[[nodiscard]] std::vector<std::size_t> AtomsCoveredByHideVolumes(
		const RendererStructureData &structure, std::span<const SceneHideVolume> volumes);

	// The volume's surface as a world-space triangle soup (every three entries are one triangle,
	// outward winding), for the translucent viewport overlay. Same frame resolution as
	// PointInHideVolume, so in `Fractional` a sphere comes out as the ellipsoid it actually cuts and
	// a box as a parallelepiped. `invert` is ignored - the surface is the same either way. Empty for
	// a singular lattice in `Fractional`, or a non-positive dimension.
	//
	// Every vertex lies on the surface: pulled 2 % towards the centre it is inside the volume,
	// pushed 2 % away it is outside (SceneHideVolumeMeshTests pins this).
	[[nodiscard]] std::vector<glm::vec3> BuildHideVolumeTriangles(
		const SceneHideVolume &volume, const RendererStructureData &structure);

	// The volume's outline as a world-space line list (every two entries are one segment), for the
	// wireframe overlay and for picking it in the viewport. Same frame resolution and the same
	// empty cases as BuildHideVolumeTriangles. Box: its 12 edges. Cylinder: both rims and four
	// lines along the axis. Sphere: three great circles in the volume's own planes (an ellipsoid's
	// in `Fractional`).
	[[nodiscard]] std::vector<glm::vec3> BuildHideVolumeEdges(
		const SceneHideVolume &volume, const RendererStructureData &structure);

	// World -> the volume's unit shape: the sphere becomes |p| <= 1, the box max|p_i| <= 1, the
	// cylinder |p.xy| <= 1 and |p.z| <= 1. Affine in both frames (the fractional map is linear), so
	// one matrix carries the test to a shader that clips an isosurface per fragment. `invert` is not
	// folded in - the caller flips the result. nullopt when BuildHideVolumeTriangles would be empty.
	[[nodiscard]] std::optional<glm::mat4> HideVolumeWorldToUnit(
		const SceneHideVolume &volume, const RendererStructureData &structure);

	// True when any active volume covers `worldPoint` (invert applied). For the few things that are
	// placed in space but are not atoms - vacancy markers, labels - checked when they are drawn or
	// picked rather than cached, since they move without a structure rebuild.
	[[nodiscard]] bool PointCoveredByHideVolumes(
		std::span<const SceneHideVolume> volumes, glm::vec3 worldPoint, const RendererStructureData &structure);

	// The volume's centre in world space: the anchor centroid if it resolves, else `center`, mapped
	// through the lattice for `Fractional`.
	[[nodiscard]] glm::vec3 HideVolumeWorldCenter(const SceneHideVolume &volume, const RendererStructureData &structure);

	// Folds `windowState.sceneHideVolumes` into the atom and bond flags of
	// `windowState.structure`, clearing them and never setting them - the manual half (H, the
	// outliner eye, HiddenSceneState) stays the owner of what is true, and this only takes away.
	// That is what keeps editing a radius from resurrecting what the user hid by hand.
	//
	// An active volume cuts both `visible` and `renderable`; its eyes only show or hide its outline.
	//
	// The mirror flags this writes are therefore NOT the manual state. Anything that records what
	// the user hid (undo snapshots, view snapshots, the outliner eye) must read VisibilityComponent
	// - CaptureHiddenSceneState(const RendererWindowState &) - or a volume's cut gets baked in as a manual H
	// and outlives the volume being shrunk or deleted.
	//
	// A bond goes with either of its endpoints - the existing H semantics, no new rule.
	//
	// Call at the end of SceneSystem::PushSelectionAndVisibilityToWindowState, on the window-state
	// mirror only, never on the ECS components. Idempotent and non-accumulating, because the mirror
	// is rebuilt from the ECS on every push: deleting a volume brings its atoms back on the next one.
	struct RendererWindowState;
	void ApplyHideVolumeMaskToWindowState(RendererWindowState &windowState);
} // namespace DefectStudio
