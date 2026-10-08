#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <glad/gl.h>

#include "Renderer/Scene/SceneHideVolume.hpp"
#include "Renderer/Scene/SceneHideVolumeSilhouette.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	void OpenGlRendererBackend::renderSceneHideVolumes(
		const std::vector<SceneHideVolume> &volumes,
		const std::vector<std::size_t> &selectedVolumes,
		const RendererStructureData &structure,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec2 &viewportPixelSize,
		const glm::vec3 &sceneOffset)
	{
		const glm::mat4 view = camera.ViewMatrix();
		const glm::mat4 viewProjection = camera.ProjectionMatrix() * view;
		const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 cameraBack(view[0][2], view[1][2], view[2][2]);
		RendererGlobalRenderSettings lineSettings = globalSettings;
		lineSettings.lighting.ambientIntensity = 1.0f;
		lineSettings.lighting.keyIntensity = lineSettings.lighting.fillIntensity = lineSettings.lighting.backIntensity = 0.0f;
		lineSettings.lighting.specularIntensity = lineSettings.lighting.rimIntensity = 0.0f;
		lineSettings.colorSaturation = 1.0f;
		for (std::size_t index = 0; index < volumes.size(); ++index)
		{
			if (!volumes[index].visible) continue;
			const SceneHideVolume &volume = volumes[index];
			const bool selected = std::find(selectedVolumes.begin(), selectedVolumes.end(), index) != selectedVolumes.end();
			const auto silhouette = BuildHideVolumeSilhouette(volume, structure, camera, sceneOffset);
			const glm::vec3 color = selected ? glm::mix(volume.color, glm::vec3(1.0f), 0.2f) : volume.color;
			const float alpha = std::clamp(volume.alpha * (selected ? 1.25f : 1.0f), 0.0f, 1.0f) *
				(volume.active ? 1.0f : 0.5f);
			const auto drawEdges = [&](const std::vector<glm::vec3> &edges, float opacity) {
				std::vector<glm::vec3> triangles;
				triangles.reserve(edges.size() * 3);
				for (std::size_t edge = 0; edge + 1 < edges.size(); edge += 2)
				{
					glm::vec3 a = edges[edge] + sceneOffset;
					glm::vec3 b = edges[edge + 1] + sceneOffset;
					if (!SelectionHitTest::ClipSegmentToNearPlane(viewProjection, a, b)) continue;
					a -= sceneOffset;
					b -= sceneOffset;
					const glm::vec3 towardCamera = camera.Projection() == CameraProjection::Perspective
						? camera.Position() - (a + b) * 0.5f - sceneOffset : cameraBack;
					const glm::vec3 side = glm::cross(b - a, towardCamera);
					if (glm::dot(side, side) <= 1.0e-12f) continue;
					const auto widthA = WorldUnitsPerPixelAt(camera, a + sceneOffset, cameraRight, viewportPixelSize);
					const auto widthB = WorldUnitsPerPixelAt(camera, b + sceneOffset, cameraRight, viewportPixelSize);
					if (!widthA || !widthB) continue;
					// Like plane frames, use quads instead of non-portable wide GL_LINES.
					const glm::vec3 unitSide = glm::normalize(side) * (selected ? 1.5f : 0.75f);
					const glm::vec3 da = unitSide * *widthA, db = unitSide * *widthB;
					triangles.insert(triangles.end(), {a - da, b - db, b + db, a - da, b + db, a + da});
				}
				renderSceneVolumeOverlay(triangles, resources.sceneHideVolumeMesh, camera, lineSettings,
					color, opacity, sceneOffset);
			};
			drawEdges(BuildHideVolumeEdges(volume, structure), silhouette.empty() ? alpha : alpha * 0.35f);
			drawEdges(silhouette, alpha);
		}
	}

	int OpenGlRendererBackend::uploadIsosurfaceHideVolumes(
		const std::vector<SceneHideVolume> &volumes, const RendererStructureData &structure)
	{
		const unsigned int program = m_ShaderLibrary.Program("isosurface");
		if (program == 0) return 0;
		glUseProgram(program);
		int count = 0;
		// ponytail: clip the first eight valid active volumes; use a buffer if more are needed.
		for (const auto &volume : volumes)
		{
			if (!volume.active) continue;
			const auto worldToUnit = HideVolumeWorldToUnit(volume, structure);
			if (!worldToUnit) continue;
			const std::string suffix = "[" + std::to_string(count) + "]";
			glUniformMatrix4fv(m_ShaderLibrary.Uniform("isosurface", "u_HideVolumeWorldToUnit" + suffix),
				1, GL_FALSE, &(*worldToUnit)[0][0]);
			glUniform1i(m_ShaderLibrary.Uniform("isosurface", "u_HideVolumeKind" + suffix), static_cast<int>(volume.kind));
			glUniform1i(m_ShaderLibrary.Uniform("isosurface", "u_HideVolumeInvert" + suffix), volume.invert ? 1 : 0);
			if (++count == 8) break;
		}
		return count;
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
