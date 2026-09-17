#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <glad/gl.h>

#include "Renderer/Scene/SceneObjectAppearance.hpp"

namespace DefectStudio
{
	namespace
	{
		void UploadOrbitalMesh(OpenGlMeshHandles &mesh, const std::vector<IsosurfaceVertex> &vertices)
		{
			if (vertices.empty())
			{
				DeleteMeshHandles(mesh);
				return;
			}

			if (mesh.vao == 0)
				glGenVertexArrays(1, &mesh.vao);
			if (mesh.vbo == 0)
				glGenBuffers(1, &mesh.vbo);

			glBindVertexArray(mesh.vao);
			glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
			glBufferData(
				GL_ARRAY_BUFFER,
				static_cast<GLsizeiptr>(vertices.size() * sizeof(IsosurfaceVertex)),
				vertices.data(),
				GL_STATIC_DRAW);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(
				0, 3, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceVertex),
				reinterpret_cast<void *>(offsetof(IsosurfaceVertex, position)));
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(
				1, 3, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceVertex),
				reinterpret_cast<void *>(offsetof(IsosurfaceVertex, normal)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(
				2, 1, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceVertex),
				reinterpret_cast<void *>(offsetof(IsosurfaceVertex, sign)));
			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			mesh.indexCount = static_cast<int>(vertices.size());
		}
	} // namespace

	void OpenGlRendererBackend::renderSceneOrbitals(
		const std::vector<RendererWindowState::SceneOrbital> &orbitals,
		const std::vector<std::size_t> &selectedOrbitals,
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec3 &sceneOffset)
	{
		for (auto cacheIt = resources.sceneOrbitalMeshCache.begin();
			cacheIt != resources.sceneOrbitalMeshCache.end();)
		{
			const SceneObjectId id = cacheIt->first;
			const bool stillPresent = std::any_of(
				orbitals.begin(), orbitals.end(), [id](const auto &orbital) { return orbital.id == id; });
			if (stillPresent)
			{
				++cacheIt;
				continue;
			}
			DeleteMeshHandles(cacheIt->second.mesh);
			cacheIt = resources.sceneOrbitalMeshCache.erase(cacheIt);
		}

		for (std::size_t orbitalIndex = 0; orbitalIndex < orbitals.size(); ++orbitalIndex)
		{
			const RendererWindowState::SceneOrbital &orbital = orbitals[orbitalIndex];
			if (!orbital.id.IsValid())
				continue;

			OpenGlSceneOrbitalMeshCache &cache = resources.sceneOrbitalMeshCache[orbital.id];
			const SceneOrbitalMeshKey key = MakeSceneOrbitalMeshKey(orbital, structure);
			if (!cache.initialized || cache.key != key)
			{
				UploadOrbitalMesh(cache.mesh, BuildSceneOrbitalMesh(orbital, structure));
				cache.key = key;
				cache.initialized = true;
			}

			if (!orbital.visible || cache.mesh.indexCount <= 0)
				continue;
			const bool selected =
				std::find(selectedOrbitals.begin(), selectedOrbitals.end(), orbitalIndex) != selectedOrbitals.end();
			renderIsosurfaceGpuOverlay(
				cache.mesh.vao,
				cache.mesh.indexCount,
				camera,
				globalSettings,
				ApplySceneSelectionHighlight(orbital.positiveLobeColor, selected),
				ApplySceneSelectionHighlight(orbital.negativeLobeColor, selected),
				orbital.alpha,
				sceneOffset);
		}
	}
} // namespace DefectStudio
