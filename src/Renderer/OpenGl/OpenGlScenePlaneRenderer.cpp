#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <vector>

#include <glad/gl.h>

#include "Renderer/Scene/ScenePlaneGeometry.hpp"

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
			const RendererWindowState::ScenePlane &plane)
		{
			const std::array<glm::vec3, 4> corners = ScenePlaneCorners(plane);
			std::vector<IsosurfaceVertex> mesh;
			mesh.reserve(plane.showBorder ? 36u : 6u);
			AppendQuad(mesh, corners[0], corners[1], corners[2], corners[3], plane.normal, 1.0f);
			if (!plane.showBorder)
				return mesh;

			// Inset frame rather than an outline drawn with GL_LINES: line width above 1px is not
			// portable, and a frame made of quads scales with the plane instead of with the screen.
			const float width =
				0.02f * std::max(0.05f, std::max(plane.halfExtents.x, plane.halfExtents.y));
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
	} // namespace

	void OpenGlRendererBackend::renderScenePlanes(
		const std::vector<RendererWindowState::ScenePlane> &planes,
		const RendererViewCamera &camera,
		OpenGlViewportResources &resources,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec3 &sceneOffset)
	{
		if (planes.empty())
			return;

		// ponytail: rebuilt and re-uploaded every frame, no per-plane mesh cache. A plane is
		// thirty-six vertices where an orbital is fourteen thousand, so the cache that pays for
		// itself there would be pure bookkeeping here. If someone ever drops a thousand planes in
		// one scene, the cache shape from renderSceneOrbitals is the thing to copy.
		std::vector<IsosurfaceVertex> mesh;
		for (const RendererWindowState::ScenePlane &plane : planes)
		{
			if (!plane.visible)
				continue;
			const std::vector<IsosurfaceVertex> planeMesh = BuildScenePlaneMesh(plane);
			mesh.insert(mesh.end(), planeMesh.begin(), planeMesh.end());
		}
		if (mesh.empty())
			return;

		OpenGlMeshHandles &handles = resources.scenePlaneMesh;
		if (handles.vao == 0)
			glGenVertexArrays(1, &handles.vao);
		if (handles.vbo == 0)
			glGenBuffers(1, &handles.vbo);

		glBindVertexArray(handles.vao);
		glBindBuffer(GL_ARRAY_BUFFER, handles.vbo);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(mesh.size() * sizeof(IsosurfaceVertex)),
			mesh.data(),
			GL_DYNAMIC_DRAW);
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
		handles.indexCount = static_cast<int>(mesh.size());

		// One draw for every plane in the window. They share the first visible plane's colour and
		// alpha, which is the cost of batching them into a single soup - per-plane colours would
		// mean one draw call each, and planes are a figure-composition tool where a consistent
		// colour is the common case. Border takes the negative slot, darkened.
		const RendererWindowState::ScenePlane *first = nullptr;
		for (const RendererWindowState::ScenePlane &plane : planes)
			if (plane.visible)
			{
				first = &plane;
				break;
			}
		if (first == nullptr)
			return;

		renderIsosurfaceGpuOverlay(
			handles.vao,
			handles.indexCount,
			camera,
			globalSettings,
			first->color,
			first->color * 0.45f,
			first->alpha,
			sceneOffset);
	}
} // namespace DefectStudio
