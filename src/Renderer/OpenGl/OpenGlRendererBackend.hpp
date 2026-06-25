#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Renderer/OpenGl/OpenGlFrameBuffer.hpp"
#include "Renderer/OpenGl/OpenGlShaderLibrary.hpp"
#include "Renderer/RendererMeshData.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	struct OpenGlMeshHandles
	{
		unsigned int vao = 0;
		unsigned int vbo = 0;
		unsigned int ebo = 0;
		unsigned int instanceVbo = 0;
		int indexCount = 0;
	};

	struct OpenGlAtomInstance
	{
		glm::vec4 positionRadius = glm::vec4(0.0f);
		glm::vec4 color = glm::vec4(1.0f);
	};

	struct OpenGlBondInstance
	{
		glm::mat4 model = glm::mat4(1.0f);
		glm::vec4 colorA = glm::vec4(1.0f);
		glm::vec4 colorB = glm::vec4(1.0f);
	};

	struct OpenGlViewportResources
	{
		OpenGlFrameBuffer frameBuffer;
		Time::SteadyTimePoint lastRenderTime{};
		bool atomsDirty = true;
		bool bondsDirty = true;
		bool gridDirty = true;
		std::size_t lastAtomCount = 0;
		std::size_t lastBondCount = 0;
		std::size_t lastSelectedCount = 0;
		std::size_t lastSelectionHash = 0;
		std::string lastSourcePath;
		std::vector<OpenGlAtomInstance> cachedAtomInstances;
		std::vector<OpenGlBondInstance> cachedBondInstances;
		std::vector<glm::vec3> cachedGridVertices;
	};

	class RendererViewCamera;

	class OpenGlRendererBackend
	{
	public:
		OpenGlRendererBackend() = default;
		~OpenGlRendererBackend();

		Result<void> Initialize(const Path &shaderDirectory, const RendererPrimitiveMeshAssets &primitiveMeshes);
		void Shutdown();
		void ReloadShadersIfNeeded();
		void CollectProfilingData();
		void MarkGridDirty();
		[[nodiscard]] unsigned int RenderWindow(
			const std::string &windowKey,
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			const RendererGlobalRenderSettings &globalSettings,
			int viewportWidth,
			int viewportHeight,
			bool showAtoms,
			bool showBonds,
			bool showCellBox,
			bool showGrid,
			const std::vector<std::size_t> &selectedAtomIndices = {});

	private:
		Result<void> createStaticGeometry(const RendererPrimitiveMeshAssets &primitiveMeshes);
		void releaseStaticGeometry();
		Result<void> createSphereMesh(const RendererStaticMeshData &meshData);
		Result<void> createCylinderMesh(const RendererStaticMeshData &meshData);
		void createScreenGrid();
		void configureOpenGlState() const;
		void renderAtoms(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			const RendererGlobalRenderSettings &globalSettings,
			const std::vector<std::size_t> &selectedIndices = {});
		void renderBonds(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			const RendererGlobalRenderSettings &globalSettings);
		void renderCellBox(const RendererStructureData &structure, const RendererViewCamera &camera);
		void renderGrid(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			const RendererGlobalRenderSettings &globalSettings);
		// T09 extension point: GPU-side bond transform via compute shader.
		// SSBO i shader są inicjalizowane, ale dispatch nie jest wywoływany.
		// Aktywować gdy T09 wprowadzi automatyczną regenerację bondów przy przesuwaniu atomów.
		void dispatchBondCompute(const RendererStructureData &structure);
		[[nodiscard]] OpenGlViewportResources &viewportResources(const std::string &windowKey, int width, int height);
		[[nodiscard]] glm::mat4 buildBondTransform(const glm::vec3 &start, const glm::vec3 &finish, float radius) const;

	private:
		bool m_Initialized = false;
		Path m_ShaderDirectory;
		OpenGlShaderLibrary m_ShaderLibrary;
		OpenGlMeshHandles m_SphereMesh;
		OpenGlMeshHandles m_CylinderMesh;
		unsigned int m_LineVao = 0;
		unsigned int m_LineVbo = 0;
		unsigned int m_ComputeInputSsbo = 0;
		unsigned int m_ComputeOutputSsbo = 0;
		std::unordered_map<std::string, OpenGlViewportResources> m_Viewports;
	};
} // namespace DefectStudio
