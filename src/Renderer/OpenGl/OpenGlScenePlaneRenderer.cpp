#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include <glad/gl.h>

#include "Renderer/Scene/ScenePlanePlacement.hpp"

namespace DefectStudio
{
	namespace
	{
		void AppendQuad(
			std::vector<IsosurfaceVertex> &out,
			const glm::vec3 &a,
			const glm::vec3 &b,
			const glm::vec3 &c,
			const glm::vec3 &d,
			const glm::vec3 &normal,
			const float sign)
		{
			const auto push = [&](const glm::vec3 &position) { out.push_back({position, normal, sign}); };
			push(a);
			push(b);
			push(c);
			push(a);
			push(c);
			push(d);
		}

		// A plane is two triangles, and the isosurface overlay already draws exactly that: a
		// translucent, two-sided triangle soup with a per-vertex +/- sign choosing between two
		// colours. Reusing it means no new shader, no new pipeline and no new uniforms - the
		// border below rides along as eight more triangles in the same soup, taking the negative
		// slot so it can be a different colour without a second draw call.
		[[nodiscard]] std::vector<IsosurfaceVertex> BuildScenePlaneMesh(
			const RendererWindowState::ScenePlane &plane, const RendererViewCamera &camera,
			const glm::vec2 &viewportPixelSize)
		{
			const std::array<glm::vec3, 4> corners = ScenePlaneCorners(plane);
			std::vector<IsosurfaceVertex> mesh;
			mesh.reserve(plane.showBorder ? 36u : 6u);
			AppendQuad(mesh, corners[0], corners[1], corners[2], corners[3], plane.normal, 1.0f);
			if (!plane.showBorder)
				return mesh;

			// Inset frame rather than an outline drawn with GL_LINES: line width above 1px is not
			// portable, and a frame made of quads scales with the plane instead of with the screen.
			// The width itself has a screen-space floor (ScenePlaneBorderWidth) so a plane seen
			// edge-on keeps a visible line instead of vanishing along with the fill (task 33).
			const float width = ScenePlaneBorderWidth(plane, camera, viewportPixelSize);
			const glm::vec3 bitangent = glm::cross(plane.normal, plane.tangent);
			const glm::vec3 insetTangent = plane.tangent * width;
			const glm::vec3 insetBitangent = bitangent * width;
			for (std::size_t i = 0; i < 4; ++i)
			{
				const glm::vec3 &from = corners[i];
				const glm::vec3 &to = corners[(i + 1) % 4];
				// Inset points toward the centre, so the frame sits inside the quad's own outline
				// and cannot z-fight with whatever the plane is lying against.
				const glm::vec3 inward =
					glm::normalize(plane.center - 0.5f * (from + to)) * width;
				AppendQuad(mesh, from, to, to + inward, from + inward, plane.normal, -1.0f);
			}
			(void)insetTangent;
			(void)insetBitangent;
			return mesh;
		}

		[[nodiscard]] std::vector<IsosurfaceVertex> BuildScenePlaneSelectionOutlineMesh(
			const RendererWindowState::ScenePlane &plane, const float width)
		{
			if (width <= 0.0f)
				return {};
			const glm::vec3 bitangent = glm::cross(plane.normal, plane.tangent);
			const glm::vec3 outerTangent = plane.tangent * (plane.halfExtents.x + width);
			const glm::vec3 outerBitangent = bitangent * (plane.halfExtents.y + width);
			const std::array<glm::vec3, 4> outer = {
				plane.center - outerTangent - outerBitangent,
				plane.center + outerTangent - outerBitangent,
				plane.center + outerTangent + outerBitangent,
				plane.center - outerTangent + outerBitangent};
			const std::array<glm::vec3, 4> inner = ScenePlaneCorners(plane);
			std::vector<IsosurfaceVertex> mesh;
			mesh.reserve(24u);
			for (std::size_t i = 0; i < 4; ++i)
				AppendQuad(mesh, outer[i], outer[(i + 1) % 4], inner[(i + 1) % 4], inner[i], plane.normal, 1.0f);
			return mesh;
		}
	} // namespace

	void OpenGlRendererBackend::renderScenePlanes(
		const std::vector<RendererWindowState::ScenePlane> &planes,
		const std::vector<std::size_t> &selectedPlanes,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec2 &viewportPixelSize,
		const glm::vec3 &sceneOffset)
	{
		if (planes.empty())
			return;

		OpenGlMeshHandles &handles = resources.scenePlaneMesh;
		if (handles.vao == 0)
			glGenVertexArrays(1, &handles.vao);
		if (handles.vbo == 0)
			glGenBuffers(1, &handles.vbo);

		// Fill back to front, then draw all selection frames so nearer fills cannot tint them.
		const auto order = ScenePlaneBackToFrontOrder(planes, camera.Position() - sceneOffset);
		for (const bool outlinePass : {false, true})
		for (const std::size_t planeIndex : order)
		{
			const auto &plane = planes[planeIndex];
			std::vector<IsosurfaceVertex> mesh;
			if (outlinePass)
			{
				if (std::find(selectedPlanes.begin(), selectedPlanes.end(), planeIndex) == selectedPlanes.end()) continue;
				const glm::vec3 bitangent = glm::cross(plane.normal, plane.tangent);
				float width = 0.0f;
				for (const auto &probe : {plane.tangent, bitangent})
					if (const auto worldPerPixel = WorldUnitsPerPixelAt(camera, plane.center, probe, viewportPixelSize))
						width = std::max(width, globalSettings.viewport.selectionOutlineWidth * *worldPerPixel);
				mesh = BuildScenePlaneSelectionOutlineMesh(plane, width);
			}
			else mesh = BuildScenePlaneMesh(plane, camera, viewportPixelSize);
			if (mesh.empty()) continue;
			glBindVertexArray(handles.vao);
			glBindBuffer(GL_ARRAY_BUFFER, handles.vbo);
			glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.size() * sizeof(IsosurfaceVertex)),
				mesh.data(), GL_DYNAMIC_DRAW);
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
				plane.color, plane.color * 0.45f, plane.alpha, sceneOffset, outlinePass, 0.0f, false, true);
		}
	}
} // namespace DefectStudio
