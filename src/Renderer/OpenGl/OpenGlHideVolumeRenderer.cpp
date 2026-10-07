#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <cstddef>
#include <vector>

#include <glad/gl.h>

#include "Renderer/Scene/SceneHideVolume.hpp"

namespace DefectStudio
{
	void OpenGlRendererBackend::renderSceneHideVolumes(
		const std::vector<SceneHideVolume> &volumes,
		const std::vector<std::size_t> &selectedVolumes,
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec3 &sceneOffset)
	{
		if (selectedVolumes.empty()) return;
		for (const std::size_t index : selectedVolumes)
		{
			if (index >= volumes.size() || !volumes[index].visible) continue;
			const SceneHideVolume &volume = volumes[index];
			const auto triangles = BuildHideVolumeTriangles(volume, structure);
			renderSceneVolumeOverlay(triangles, resources.sceneHideVolumeMesh, camera, globalSettings,
				volume.color, volume.alpha, sceneOffset);
		}
	}

	void OpenGlRendererBackend::renderSceneVolumeOverlay(
		const std::vector<glm::vec3> &triangles,
		OpenGlMeshHandles &handles,
		const RendererViewCamera &camera,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec3 &color, const float alpha,
		const glm::vec3 &sceneOffset)
	{
		std::vector<IsosurfaceVertex> mesh;
		mesh.reserve(triangles.size());
		for (std::size_t vertex = 0; vertex + 2 < triangles.size(); vertex += 3)
		{
			const glm::vec3 normal = glm::cross(triangles[vertex + 1] - triangles[vertex], triangles[vertex + 2] - triangles[vertex]);
			if (glm::dot(normal, normal) <= 0.0f) continue;
			const glm::vec3 unitNormal = glm::normalize(normal);
			for (std::size_t corner = 0; corner < 3; ++corner)
				mesh.push_back({triangles[vertex + corner], unitNormal, 1.0f});
		}
		if (mesh.empty()) return;
		if (handles.vao == 0) glGenVertexArrays(1, &handles.vao);
		if (handles.vbo == 0) glGenBuffers(1, &handles.vbo);
		glBindVertexArray(handles.vao);
		glBindBuffer(GL_ARRAY_BUFFER, handles.vbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.size() * sizeof(IsosurfaceVertex)), mesh.data(), GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceVertex),
			reinterpret_cast<void *>(offsetof(IsosurfaceVertex, position)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceVertex),
			reinterpret_cast<void *>(offsetof(IsosurfaceVertex, normal)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(IsosurfaceVertex),
			reinterpret_cast<void *>(offsetof(IsosurfaceVertex, sign)));
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		handles.indexCount = static_cast<int>(mesh.size());
		renderIsosurfaceGpuOverlay(handles.vao, handles.indexCount, camera, globalSettings,
			color, color * 0.45f, alpha, sceneOffset, false, 0.0f, false, true);
	}
} // namespace DefectStudio
