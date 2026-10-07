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
		glm::vec3 color = glm::vec3(0.9f, 0.5f, 0.2f);
		float alpha = 0.18f;
		// The Scene Outliner's two columns, as everywhere else: `visible` is the eye (the volume is
		// drawn in the viewport AND its mask applies), `renderable` is the camera.
		bool visible = true;
		bool renderable = true;
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

	// Indices into `structure.atoms` covered by at least one volume with `visible` set, ascending and
	// unique. Volumes with the eye cleared contribute nothing. Evaluated when a volume changes or the
	// structure is rebuilt - atom positions only move on a rebuild, so this is not per-frame work.
	[[nodiscard]] std::vector<std::size_t> AtomsCoveredByHideVolumes(
		const RendererStructureData &structure, std::span<const SceneHideVolume> volumes);

	// Folds `windowState.sceneHideVolumes` into the atom and bond flags of
	// `windowState.structure`, clearing them and never setting them - the manual half (H, the
	// outliner eye, HiddenSceneState) stays the owner of what is true, and this only takes away.
	// That is what keeps editing a radius from resurrecting what the user hid by hand.
	//
	// The volume's own two columns map straight onto the atoms' two: a volume with the eye set cuts
	// `visible`, one with the camera set cuts `renderable`. So a volume can drop the bulk from an
	// exported render while leaving it on screen to work with.
	//
	// A bond goes with either of its endpoints - the existing H semantics, no new rule.
	//
	// Call at the end of SceneSystem::PushSelectionAndVisibilityToWindowState, on the window-state
	// mirror only, never on the ECS components. Idempotent and non-accumulating, because the mirror
	// is rebuilt from the ECS on every push: deleting a volume brings its atoms back on the next one.
	struct RendererWindowState;
	void ApplyHideVolumeMaskToWindowState(RendererWindowState &windowState);
} // namespace DefectStudio
