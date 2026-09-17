#include "Core/dspch.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio
{
	// STUB - not implemented yet. The header and
	// tests/Renderer/Scene/SceneOrbitalGeometryTests.cpp are the contract; every function returns a
	// neutral value so the test binary links and the tests fail on behaviour, not on the linker.

	OrbitalWavefunction BuildOrbitalWavefunction(
		const RendererWindowState::SceneOrbital & /*orbital*/, const RendererStructureData & /*structure*/)
	{
		return OrbitalWavefunction{};
	}

	SceneOrbitalCenters ResolveSceneOrbitalCenters(
		const RendererWindowState::SceneOrbital & /*orbital*/, const RendererStructureData & /*structure*/)
	{
		return SceneOrbitalCenters{};
	}

	std::vector<IsosurfaceVertex> BuildSceneOrbitalMesh(
		const RendererWindowState::SceneOrbital & /*orbital*/, const RendererStructureData & /*structure*/)
	{
		return {};
	}

	SceneOrbitalMeshKey MakeSceneOrbitalMeshKey(
		const RendererWindowState::SceneOrbital & /*orbital*/, const RendererStructureData & /*structure*/)
	{
		return SceneOrbitalMeshKey{};
	}

	void ResolveAnchoredOrbitals(RendererWindowState & /*windowState*/)
	{
	}

	RendererWindowState::SceneOrbital MakeDefaultSceneOrbital(
		const RendererWindowState & /*windowState*/, OrbitalPreset preset, const glm::vec3 & /*seedPosition*/)
	{
		RendererWindowState::SceneOrbital orbital;
		orbital.preset = preset;
		return orbital;
	}

	float ValenceEffectiveCharge(const std::string & /*element*/)
	{
		return 1.0f;
	}

	int ValenceShell(const std::string & /*element*/)
	{
		return 1;
	}
} // namespace DefectStudio
