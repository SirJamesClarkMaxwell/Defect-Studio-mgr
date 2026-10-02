#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// task/52: point a single-centre orbital's lobe at something - a vacancy, an atom about to be
	// removed, the 3D cursor. The answer is written into SceneOrbital::rotationEuler once; nothing
	// keeps it pointing afterwards.
	//   ponytail: one-shot. A persisted aim target re-resolved every frame (the anchorAtoms rule)
	//   is the upgrade if relaxation playback ever needs lobes that keep pointing.

	// The rotationEuler (degrees, the RotationFrame / RotatedEulerDegrees convention -
	// glm::quat(radians(euler))) under which the orbital's member axis (OrbitalPresetMemberAxis of
	// its preset and lobeIndex) points from `centre` toward `target`.
	//
	// Composed as the MINIMAL rotation taking the current world axis onto the target direction,
	// applied on top of the current orientation - so whatever twist about the lobe axis the user
	// had is kept, and aiming an already-aimed orbital changes nothing. Antiparallel is a half turn
	// about any perpendicular axis, never a NaN.
	//
	// nullopt - and the caller leaves the orbital alone - for a two-centre preset (its orientation
	// comes from its centres), for a member without an axis (s, most f), and when target and centre
	// are closer than 1e-4 A.
	[[nodiscard]] std::optional<glm::vec3> AimSceneOrbitalEuler(
		const RendererWindowState::SceneOrbital &orbital, const glm::vec3 &centre, const glm::vec3 &target);

	// Something an orbital can be aimed at, as the properties panel lists it.
	struct OrbitalAimTarget
	{
		std::string label;
		glm::vec3 position = glm::vec3(0.0f);
	};

	// The panel's target list, in this order:
	//   every vacancy of the window's structure  - label "<GetLabel()> #<i+1>", e.g. "V_C #1"
	//   every selected atom, in selection order  - label "<element> #<index+1>", e.g. "N #64"
	//   the 3D cursor                            - label "Kursor 3D"
	// Stale selection indices (>= atom count) are skipped. The cursor is always last and always
	// present, so the list is never empty.
	[[nodiscard]] std::vector<OrbitalAimTarget> CollectOrbitalAimTargets(const RendererWindowState &windowState);

	// Where "dangling bonds -> vacancy" points the lobes of `atoms`: the vacancy nearest to the
	// atoms' centroid when the structure has any, otherwise the centroid itself. Stale indices are
	// ignored; nullopt when no index resolves.
	[[nodiscard]] std::optional<glm::vec3> ResolveDanglingBondTarget(
		const RendererWindowState &windowState, const std::vector<std::size_t> &atoms);

	// One sp3 orbital per resolvable atom, in `atoms` order: MakeDefaultSceneOrbital(windowState,
	// Sp3, ..., {atom}) - so anchored to it, sized from its element - with lobeIndex 0 and
	// rotationEuler from AimSceneOrbitalEuler toward `target`. An atom sitting on the target (no
	// direction) gets the default orientation rather than being dropped. Ids and persist keys are
	// left default: the caller registers the orbitals like any other new one.
	[[nodiscard]] std::vector<RendererWindowState::SceneOrbital> MakeDanglingBondOrbitals(
		const RendererWindowState &windowState, const std::vector<std::size_t> &atoms, const glm::vec3 &target);
} // namespace DefectStudio
