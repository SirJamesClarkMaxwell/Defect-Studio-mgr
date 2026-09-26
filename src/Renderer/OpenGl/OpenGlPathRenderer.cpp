#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glad/gl.h>

#if defined(TRACY_ENABLE)
#include <tracy/TracyOpenGL.hpp>
#endif

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathLod.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"
#include "Renderer/Path/PathTessellator.hpp"
#include "Renderer/Scene/SceneObjectAppearance.hpp"

namespace DefectStudio
{
	namespace
	{
		void UploadTube(OpenGlMeshHandles &mesh, const StrokeGeometry &geometry)
		{
			if (mesh.vao == 0)
				glGenVertexArrays(1, &mesh.vao);
			if (mesh.vbo == 0)
				glGenBuffers(1, &mesh.vbo);
			if (mesh.ebo == 0)
				glGenBuffers(1, &mesh.ebo);
			glBindVertexArray(mesh.vao);
			glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
			glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(geometry.tubeVertices.size() * sizeof(StrokeTubeVertex)), geometry.tubeVertices.data(), GL_STATIC_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(geometry.indices.size() * sizeof(std::uint32_t)), geometry.indices.data(), GL_STATIC_DRAW);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(StrokeTubeVertex), reinterpret_cast<void *>(offsetof(StrokeTubeVertex, position)));
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(StrokeTubeVertex), reinterpret_cast<void *>(offsetof(StrokeTubeVertex, normal)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(StrokeTubeVertex), reinterpret_cast<void *>(offsetof(StrokeTubeVertex, color)));
			glBindVertexArray(0);
			mesh.indexCount = static_cast<int>(geometry.indices.size());
		}

		void UploadRibbon(OpenGlMeshHandles &mesh, const StrokeGeometry &geometry)
		{
			if (mesh.vao == 0)
				glGenVertexArrays(1, &mesh.vao);
			if (mesh.vbo == 0)
				glGenBuffers(1, &mesh.vbo);
			if (mesh.ebo == 0)
				glGenBuffers(1, &mesh.ebo);
			glBindVertexArray(mesh.vao);
			glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
			glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(geometry.ribbonVertices.size() * sizeof(StrokeRibbonVertex)), geometry.ribbonVertices.data(), GL_STATIC_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(geometry.indices.size() * sizeof(std::uint32_t)), geometry.indices.data(), GL_STATIC_DRAW);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(StrokeRibbonVertex), reinterpret_cast<void *>(offsetof(StrokeRibbonVertex, position)));
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(StrokeRibbonVertex), reinterpret_cast<void *>(offsetof(StrokeRibbonVertex, tangent)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(StrokeRibbonVertex), reinterpret_cast<void *>(offsetof(StrokeRibbonVertex, normal)));
			glEnableVertexAttribArray(3);
			glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(StrokeRibbonVertex), reinterpret_cast<void *>(offsetof(StrokeRibbonVertex, color)));
			glEnableVertexAttribArray(4);
			glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(StrokeRibbonVertex), reinterpret_cast<void *>(offsetof(StrokeRibbonVertex, side)));
			glEnableVertexAttribArray(5);
			glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(StrokeRibbonVertex), reinterpret_cast<void *>(offsetof(StrokeRibbonVertex, halfWidth)));
			glBindVertexArray(0);
			mesh.indexCount = static_cast<int>(geometry.indices.size());
		}

		[[nodiscard]] bool ProjectNdc(const glm::mat4 &viewProjection, const glm::vec3 &position, glm::vec3 &ndc)
		{
			const glm::vec4 clip = viewProjection * glm::vec4(position, 1.0f);
			if (clip.w <= 0.0f)
				return false;
			ndc = glm::vec3(clip) / clip.w;
			return true;
		}

		void UploadLighting(OpenGlShaderLibrary &library, const std::string &programName, const RendererViewCamera &camera, const RendererGlobalRenderSettings &settings, const glm::mat4 &viewProjection, const glm::vec3 &sceneOffset)
		{
			const auto uniform = [&](const char *name) { return library.Uniform(programName, name); };
			const int viewProjectionLocation = uniform("u_ViewProjection");
			if (viewProjectionLocation >= 0)
				glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
			const int sceneOffsetLocation = uniform("u_SceneOffset");
			if (sceneOffsetLocation >= 0)
				glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);
			const int keyDirectionLocation = uniform("u_KeyDirection");
			const int fillDirectionLocation = uniform("u_FillDirection");
			const int backDirectionLocation = uniform("u_BackDirection");
			if (keyDirectionLocation >= 0)
				glUniform3fv(keyDirectionLocation, 1, &settings.lighting.keyDirection.x);
			if (fillDirectionLocation >= 0)
				glUniform3fv(fillDirectionLocation, 1, &settings.lighting.fillDirection.x);
			if (backDirectionLocation >= 0)
				glUniform3fv(backDirectionLocation, 1, &settings.lighting.backDirection.x);
			const int ambientLocation = uniform("u_AmbientIntensity");
			const int keyIntensityLocation = uniform("u_KeyIntensity");
			const int fillIntensityLocation = uniform("u_FillIntensity");
			const int backIntensityLocation = uniform("u_BackIntensity");
			if (ambientLocation >= 0) glUniform1f(ambientLocation, settings.lighting.ambientIntensity);
			if (keyIntensityLocation >= 0) glUniform1f(keyIntensityLocation, settings.lighting.keyIntensity);
			if (fillIntensityLocation >= 0) glUniform1f(fillIntensityLocation, settings.lighting.fillIntensity);
			if (backIntensityLocation >= 0) glUniform1f(backIntensityLocation, settings.lighting.backIntensity);
			const int twoSidedLocation = uniform("u_TwoSidedLighting");
			if (twoSidedLocation >= 0) glUniform1i(twoSidedLocation, settings.lighting.twoSided ? 1 : 0);
			const glm::vec3 cameraPosition = camera.Position();
			const int cameraPositionLocation = uniform("u_CameraPosition");
			if (cameraPositionLocation >= 0) glUniform3fv(cameraPositionLocation, 1, &cameraPosition.x);
			const int specularIntensityLocation = uniform("u_SpecularIntensity");
			const int shininessLocation = uniform("u_Shininess");
			const int saturationLocation = uniform("u_Saturation");
			const int specularScaleLocation = uniform("u_SpecularScale");
			if (specularIntensityLocation >= 0) glUniform1f(specularIntensityLocation, settings.lighting.specularIntensity);
			if (shininessLocation >= 0) glUniform1f(shininessLocation, settings.lighting.shininess);
			if (saturationLocation >= 0) glUniform1f(saturationLocation, settings.colorSaturation);
			if (specularScaleLocation >= 0) glUniform1f(specularScaleLocation, 0.25f);
		}
	} // namespace

	void OpenGlRendererBackend::renderScenePaths(const PathRenderInput &input, const RendererViewCamera &camera, OpenGlViewportResources &resources, const RendererGlobalRenderSettings &globalSettings, const bool renderAlwaysOnTop, const glm::vec2 &viewportPixelSize, const glm::vec3 &sceneOffset)
	{
		if (input.paths == nullptr)
			return;

		PathSystem &system = *input.paths;
		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		const glm::mat4 view = camera.ViewMatrix();
		const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
		const std::vector<SceneObjectId> emptySelection;
		const std::vector<SceneObjectId> &selection = input.selected == nullptr ? emptySelection : *input.selected;
		struct DrawJob { SceneObjectId id; bool tube; bool cameraFacing; float halfWidth; float alpha; bool selected; };
		std::vector<DrawJob> jobs;
		bool anyTransparent = false;

		system.Store().Visit([&](const ScenePath &path) {
			if (!path.visible || !path.renderable || path.nodes.size() < 2 || path.style.depthMode == (renderAlwaysOnTop ? PathDepthMode::DepthTest : PathDepthMode::AlwaysOnTop))
				return;
			glm::vec3 centroid(0.0f);
			for (const PathNode &node : path.nodes)
				centroid += node.position;
			centroid /= static_cast<float>(path.nodes.size());
			glm::vec3 centerNdc(0.0f), rightNdc(0.0f);
			// NDC spans [-1, 1] across the viewport, so half the extent is one viewport in pixels; z
			// plays no part in a screen-density probe and is dropped rather than folded into the length.
			const double pixelsPerWorldUnit =
				ProjectNdc(viewProjection, centroid + sceneOffset, centerNdc)
					&& ProjectNdc(viewProjection, centroid + cameraRight + sceneOffset, rightNdc)
				? static_cast<double>(glm::length((glm::vec2(rightNdc) - glm::vec2(centerNdc)) * 0.5f * viewportPixelSize))
				: 0.0;
			const auto found = resources.scenePathMeshCache.find(path.id);
			const int previousBucket = found == resources.scenePathMeshCache.end() ? kNoLodBucket : found->second.key.lodBucket;
			const int lodBucket = QuantiseLod(pixelsPerWorldUnit, previousBucket);
			// S7 resolves against an empty binding context; atom/object binding wiring belongs to S14.
			const PathEvaluationKey key{system.Store().RevisionsFor(path.id), 0, lodBucket};
			const CachedPathGeometry *cached = system.Caches().Find(path.id, key);
			if (cached == nullptr)
			{
				const ResolvedNodes resolved = ResolveNodePositions(path, BindingContext{});
				// Style revision invalidates the mesh and, for Flat, this field also changes tessellation.
				const FrameSeed frameSeed = path.style.profile == StrokeProfile::Flat
					? FrameSeed{FrameSeed::Mode::FixedNormal, glm::dvec3(path.style.ribbonNormal)} : FrameSeed{};
				const EvaluatedPath evaluated = Tessellate(path, resolved,
					TessellationSettings{ToleranceForLod(lodBucket, 0.5), 12, 4096, frameSeed});
				cached = &system.Caches().Store(path.id, key, CachedPathGeometry{evaluated, BuildStroke(evaluated, path.style)});
			}
			if (cached->stroke.indices.empty())
				return;
			OpenGlScenePathMeshCache &entry = resources.scenePathMeshCache[path.id];
			if (entry.mesh.vao == 0 || entry.key != key)
			{
				DeleteMeshHandles(entry.mesh);
				if (path.style.profile == StrokeProfile::Round)
					UploadTube(entry.mesh, cached->stroke);
				else
					UploadRibbon(entry.mesh, cached->stroke);
				entry.key = key;
			}
			if (entry.mesh.indexCount > 0)
			{
				const bool tube = path.style.profile == StrokeProfile::Round;
				const bool selected = std::find(selection.begin(), selection.end(), path.id) != selection.end();
				jobs.push_back({path.id, tube, path.style.profile == StrokeProfile::CameraFacing, path.style.width * 0.5f, path.style.alpha, selected});
				const bool gradientTransparent = path.style.gradient.enabled && std::any_of(
					path.style.gradient.stops.begin(), path.style.gradient.stops.end(), [](const PathGradientStop &stop) {
						return stop.alpha < 0.999f;
					});
				anyTransparent = anyTransparent || path.style.alpha < 0.999f || gradientTransparent;
			}
		});

		if (!renderAlwaysOnTop)
		{
			for (auto it = resources.scenePathMeshCache.begin(); it != resources.scenePathMeshCache.end();)
			{
				if (!system.Store().Contains(it->first))
				{
					DeleteMeshHandles(it->second.mesh);
					it = resources.scenePathMeshCache.erase(it);
				}
				else
					++it;
			}
		}
		if (jobs.empty())
			return;

#if defined(TRACY_ENABLE)
		TracyGpuZone("Renderer.ScenePaths");
#endif
		GLboolean previousDepthMask = GL_FALSE;
		glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
		const GLboolean previousCull = glIsEnabled(GL_CULL_FACE);
		const GLboolean previousDepth = glIsEnabled(GL_DEPTH_TEST);
		GLint previousProgram = 0;
		GLint previousVertexArray = 0;
		glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
		if (renderAlwaysOnTop)
			glDisable(GL_DEPTH_TEST);
		if (!renderAlwaysOnTop && anyTransparent)
			glDepthMask(GL_FALSE);

		const unsigned int tubeProgram = m_ShaderLibrary.Program("path_tube");
		const unsigned int ribbonProgram = m_ShaderLibrary.Program("path_ribbon");
		unsigned int activeProgram = 0;
		for (const DrawJob &job : jobs)
		{
			const unsigned int program = job.tube ? tubeProgram : ribbonProgram;
			if (program == 0)
				continue;
			if (program != activeProgram)
			{
				activeProgram = program;
				glUseProgram(program);
				UploadLighting(m_ShaderLibrary, job.tube ? "path_tube" : "path_ribbon", camera, globalSettings, viewProjection, sceneOffset);
			}
			// Per job, not inside UploadLighting: that runs once per program switch, while several
			// paths share a program and only some of them are selected.
			const char *programName = job.tube ? "path_tube" : "path_ribbon";
			const int highlightLocation = m_ShaderLibrary.Uniform(programName, "u_SelectionHighlight");
			const int strengthLocation = m_ShaderLibrary.Uniform(programName, "u_SelectionStrength");
			const glm::vec3 highlight = SceneSelectionHighlightColor();
			if (highlightLocation >= 0)
				glUniform3fv(highlightLocation, 1, &highlight.x);
			if (strengthLocation >= 0)
				glUniform1f(strengthLocation, job.selected ? kSceneSelectionHighlightStrength : 0.0f);

			const auto found = resources.scenePathMeshCache.find(job.id);
			if (found == resources.scenePathMeshCache.end())
				continue;
			const OpenGlScenePathMeshCache &entry = found->second;
			if (!job.tube)
			{
				const int halfWidth = m_ShaderLibrary.Uniform("path_ribbon", "u_HalfWidth");
				const int cameraFacing = m_ShaderLibrary.Uniform("path_ribbon", "u_CameraFacing");
				const int cameraPosition = m_ShaderLibrary.Uniform("path_ribbon", "u_CameraPosition");
				const glm::vec3 position = camera.Position();
				if (halfWidth >= 0) glUniform1f(halfWidth, job.halfWidth);
				if (cameraFacing >= 0) glUniform1i(cameraFacing, job.cameraFacing ? 1 : 0);
				if (cameraPosition >= 0) glUniform3fv(cameraPosition, 1, &position.x);
				glDisable(GL_CULL_FACE);
			}
			else if (previousCull)
				glEnable(GL_CULL_FACE);
			else
				glDisable(GL_CULL_FACE);
			glBindVertexArray(entry.mesh.vao);
			glDrawElements(GL_TRIANGLES, entry.mesh.indexCount, GL_UNSIGNED_INT, nullptr);
		}
		glBindVertexArray(static_cast<unsigned int>(previousVertexArray));
		glUseProgram(static_cast<unsigned int>(previousProgram));
		if (previousCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
		if (previousDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
		glDepthMask(previousDepthMask);
	}
} // namespace DefectStudio
