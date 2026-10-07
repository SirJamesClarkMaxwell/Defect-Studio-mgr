#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <algorithm>
#include <optional>

#include <glad/gl.h>

#include "Renderer/Scene/ScenePlaneGeometry.hpp"

namespace DefectStudio
{
	void OpenGlRendererBackend::renderSceneDensities(
		const std::vector<RendererWindowState::SceneDensity> &densities,
		const std::vector<std::size_t> &selectedDensities,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec3 &sceneOffset,
		const glm::vec2 &viewportPixelSize)
	{
		for (auto cacheIt = resources.sceneDensityMeshCache.begin(); cacheIt != resources.sceneDensityMeshCache.end();)
		{
			const SceneObjectId id = cacheIt->first;
			if (std::any_of(densities.begin(), densities.end(), [id](const auto &density) { return density.id == id; }))
			{
				++cacheIt;
				continue;
			}
			DeleteIsosurfaceGpuBuffers(cacheIt->second.buffers);
			cacheIt = resources.sceneDensityMeshCache.erase(cacheIt);
		}

		for (std::size_t index = 0; index < densities.size(); ++index)
		{
			const RendererWindowState::SceneDensity &density = densities[index];
			if (!density.id.IsValid() || density.data == nullptr || !density.visible)
				continue;

			OpenGlSceneDensityMeshCache &cache = resources.sceneDensityMeshCache[density.id];
			const bool negativeLobe = density.showNegative;
			if (cache.grid != density.data.get() || cache.isoValue != density.isoValue ||
				cache.negativeLobe != negativeLobe)
			{
				if (cache.buffers.vao == 0)
					CreateIsosurfaceGpuBuffers(cache.buffers);
				cache.vertexCount = dispatchIsosurfaceCompute(density.data->grid, density.isoValue, cache.buffers, negativeLobe);
				cache.grid = density.data.get();
				cache.isoValue = density.isoValue;
				cache.negativeLobe = negativeLobe;
			}
			if (cache.vertexCount <= 0)
				continue;

			const bool selected =
				std::find(selectedDensities.begin(), selectedDensities.end(), index) != selectedDensities.end();
			if (selected)
			{
				const glm::mat3 &cell = density.data->grid.cell;
				const glm::vec3 cellCentre = 0.5f * (cell[0] + cell[1] + cell[2]);
				const glm::mat4 view = camera.ViewMatrix();
				const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
				const std::optional<float> worldPerPixel =
					WorldUnitsPerPixelAt(camera, cellCentre, cameraRight, viewportPixelSize);
				const float outlineExpansion =
					worldPerPixel.has_value() ? globalSettings.viewport.selectionOutlineWidth * *worldPerPixel : 0.0f;
				renderIsosurfaceGpuOverlay(cache.buffers.vao, cache.vertexCount, camera, globalSettings,
					density.positiveColor, density.negativeColor, density.alpha, sceneOffset, true, outlineExpansion);
			}
			renderIsosurfaceGpuOverlay(cache.buffers.vao, cache.vertexCount, camera, globalSettings,
				density.positiveColor, density.negativeColor, density.alpha, sceneOffset);
		}
	}
} // namespace DefectStudio
