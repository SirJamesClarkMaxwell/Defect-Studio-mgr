#include "Renderer/Scene/SceneHideVolume.hpp"

namespace DefectStudio
{
	// STUB - task 84 step 1 implements this. Present so the contract tests in
	// tests/Renderer/Scene/SceneHideVolumeTests.cpp link and fail as assertions rather than as a
	// linker error. Every function returns "covers nothing".

	std::optional<glm::vec3> ResolveHideVolumeCenter(const SceneHideVolume &, const RendererStructureData &)
	{
		return std::nullopt;
	}

	bool PointInHideVolume(const SceneHideVolume &, glm::vec3, const glm::mat3 &, const RendererStructureData &)
	{
		return false;
	}

	std::vector<std::size_t> AtomsCoveredByHideVolumes(
		const RendererStructureData &, std::span<const SceneHideVolume>)
	{
		return {};
	}
} // namespace DefectStudio
