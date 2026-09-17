#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Domain/Electronic/HydrogenicOrbital.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/IsosurfaceMesher.hpp"

namespace DefectStudio
{
	// Turns one SceneOrbital's parameters into the wavefunction the domain evaluator understands.
	// Centres come from the orbital's own centerA/centerB unless anchorAtoms resolve, in which case
	// the atoms win - that is what makes an anchored orbital follow its atom. An anchor index that
	// is out of range is ignored rather than clamped, so a stale anchor degrades to the stored
	// centre instead of snapping to atom 0.
	[[nodiscard]] OrbitalWavefunction BuildOrbitalWavefunction(
		const RendererWindowState::SceneOrbital &orbital, const RendererStructureData &structure);

	// Resolved world-space centres of an orbital, after anchors are applied - what the properties
	// panel shows, what the gizmo pivots around, and what the mesh is scaled about.
	struct SceneOrbitalCenters
	{
		glm::vec3 centerA = glm::vec3(0.0f);
		glm::vec3 centerB = glm::vec3(0.0f);
		// Midpoint for a two-centre preset, centerA for a single-centre one. The pivot.
		glm::vec3 centroid = glm::vec3(0.0f);
	};

	[[nodiscard]] SceneOrbitalCenters ResolveSceneOrbitalCenters(
		const RendererWindowState::SceneOrbital &orbital, const RendererStructureData &structure);

	// Samples the orbital, extracts both phase lobes and applies SceneOrbital::scale about the
	// centroid. Returns flat GL_TRIANGLES triplets exactly as GenerateIsosurfaceMesh does, with
	// IsosurfaceVertex::sign carrying the phase - so the existing isosurface shader draws this with
	// no change. Empty when the orbital cannot be meshed (degenerate resolution, zero extent, an
	// iso value no sample reaches).
	//
	// This runs on the main thread whenever a parameter changes, NOT every frame - see
	// SceneOrbitalMeshKey below for how a caller knows when to re-run it.
	//
	// ponytail: CPU meshing, reusing GenerateIsosurfaceMesh rather than the GPU compute path the
	// WAVECAR overlay uses. That path keeps two ~64MB vertex buffers per window and is built to be
	// re-dispatched every frame while the user scrubs an iso value - correct for one big grid,
	// wasteful for a dozen small drawing orbitals that only change when someone edits them. The
	// ceiling is the re-bake hitch: at the default 48^3 it is not noticeable, at 128^3 dragging a
	// slider will stutter. The upgrade is to move the bake onto JobSystem, not to grow the slot
	// array.
	[[nodiscard]] std::vector<IsosurfaceVertex> BuildSceneOrbitalMesh(
		const RendererWindowState::SceneOrbital &orbital, const RendererStructureData &structure);

	// Everything BuildSceneOrbitalMesh reads, hashed - the renderer's per-orbital mesh cache keeps
	// one of these beside each baked mesh and re-bakes only when it changes. Colours, alpha and
	// visibility are deliberately NOT part of it: they are shader uniforms, so changing them must
	// not throw the mesh away.
	struct SceneOrbitalMeshKey
	{
		std::uint64_t hash = 0;

		[[nodiscard]] bool operator==(const SceneOrbitalMeshKey &other) const
		{
			return hash == other.hash;
		}
	};

	[[nodiscard]] SceneOrbitalMeshKey MakeSceneOrbitalMeshKey(
		const RendererWindowState::SceneOrbital &orbital, const RendererStructureData &structure);

	// Overwrites centerA (and centerB, for a two-atom anchor) from the anchored atoms, once per
	// frame, next to ResolveAnchoredBonds. Orbitals whose anchors are empty or unresolvable are
	// left alone.
	void ResolveAnchoredOrbitals(RendererWindowState &windowState);

	// A new orbital of the given preset, placed sensibly for the window's current state: anchored
	// to the selection when a two-centre preset meets exactly two selected atoms (or a single-centre
	// preset meets exactly one), otherwise free-standing at `seedPosition`. Its effectiveCharge and
	// shell come from the anchored atoms' elements where there are any, so an orbital dropped on a
	// carbon is drawn at carbon's size rather than hydrogen's.
	[[nodiscard]] RendererWindowState::SceneOrbital MakeDefaultSceneOrbital(
		const RendererWindowState &windowState, OrbitalPreset preset, const glm::vec3 &seedPosition);

	// Slater effective nuclear charge for an element's valence shell, and the shell that valence
	// sits in - what MakeDefaultSceneOrbital uses so a preset dropped on an atom starts at a
	// plausible size. Unknown elements fall back to hydrogen (1.0, shell 1).
	[[nodiscard]] float ValenceEffectiveCharge(const std::string &element);
	[[nodiscard]] int ValenceShell(const std::string &element);
} // namespace DefectStudio
