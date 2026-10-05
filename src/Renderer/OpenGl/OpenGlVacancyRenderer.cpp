#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <algorithm>
#include <glad/gl.h>

#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/VacancyMarkerGeometry.hpp"

namespace DefectStudio
{
	void OpenGlRendererBackend::renderVacancyMarkers(
		const std::vector<RendererVacancyData> &vacancies, const RendererViewCamera &camera,
		OpenGlViewportResources &resources, const RendererGlobalRenderSettings &globalSettings,
		const glm::vec2 &viewportPixelSize, const glm::vec3 &sceneOffset)
	{
		if (vacancies.empty())
			return;
		const glm::mat4 view = camera.ViewMatrix();
		const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
		OpenGlMeshHandles &handles = resources.vacancyMesh;
		if (handles.vao == 0)
			glGenVertexArrays(1, &handles.vao);
		if (handles.vbo == 0)
			glGenBuffers(1, &handles.vbo);
		RendererGlobalRenderSettings markerSettings = globalSettings;
		markerSettings.lighting.ambientIntensity = 1.0f;
		markerSettings.lighting.keyIntensity = 0.0f;
		markerSettings.lighting.fillIntensity = 0.0f;
		markerSettings.lighting.backIntensity = 0.0f;
		markerSettings.lighting.specularIntensity = 0.0f;
		markerSettings.lighting.rimIntensity = 0.0f;
		markerSettings.colorSaturation = 1.0f;
		const auto draw = [&](const std::vector<IsosurfaceVertex> &soup, const glm::vec3 &color, float alpha) {
			if (soup.empty())
				return;
			glBindVertexArray(handles.vao);
			glBindBuffer(GL_ARRAY_BUFFER, handles.vbo);
			glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(soup.size() * sizeof(IsosurfaceVertex)),
				soup.data(), GL_DYNAMIC_DRAW);
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
			handles.indexCount = static_cast<int>(soup.size());
			renderIsosurfaceGpuOverlay(handles.vao, handles.indexCount, camera, markerSettings,
				color, color, alpha, sceneOffset);
		};
		// One draw pair per marker, so each vacancy keeps its own colour (V_B / V_N, own colours).
		//   ponytail: two draws per vacancy; batch by colour if structures ever carry hundreds.
		for (const auto &vacancy : vacancies)
		{
			if (vacancy.hidden)
				continue;
			float width = vacancy.ringWidth;
			for (const auto &axis : {right, up})
				if (const auto worldPerPixel = WorldUnitsPerPixelAt(
					camera, vacancy.cartesianPosition + sceneOffset, axis, viewportPixelSize))
					width = std::max(width, 1.5f * *worldPerPixel);
			const auto mesh = BuildVacancyMarkerMesh(vacancy, right, up, width);
			draw(mesh.fill, vacancy.color, vacancy.renderMode == VacancyRenderMode::Solid ? 1.0f : vacancy.opacity);
			draw(mesh.ring, vacancy.color * 0.6f, 1.0f);
		}
	}
} // namespace DefectStudio
