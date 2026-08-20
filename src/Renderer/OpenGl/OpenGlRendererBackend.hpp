#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Renderer/OpenGl/OpenGlFrameBuffer.hpp"
#include "Renderer/OpenGl/OpenGlShaderLibrary.hpp"
#include "Renderer/RendererMeshData.hpp"
#include "Renderer/Scene/IsosurfaceMesher.hpp"
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
		bool cellEdgesDirty = true;
		std::size_t lastAtomCount = 0;
		std::size_t lastBondCount = 0;
		std::size_t lastSelectedCount = 0;
		std::size_t lastSelectionHash = 0;
		std::size_t lastVisibilityHash = 0;
		std::string lastSourcePath;
		std::vector<OpenGlAtomInstance> cachedAtomInstances;
		std::vector<OpenGlBondInstance> cachedBondInstances;
		std::vector<glm::vec3> cachedGridVertices;
		std::vector<glm::vec3> cachedCellEdgeVertices;
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
			const std::vector<std::size_t> &selectedAtomIndices = {},
			// TODO(T08.6.3): temporary debug overlays to validate the isosurface pipeline
			// end-to-end (CPU reference, then its GPU compute-shader port) before the real
			// orbital panel exists. Not part of the real per-window structure render path.
			const std::vector<IsosurfaceVertex> *debugIsosurfaceMesh = nullptr,
			int debugGpuIsosurfaceVertexCount = 0);

		// Runs the marching-tetrahedra compute shader (isosurface_march.comp - GPU port of
		// GenerateIsosurfaceMesh) over `grid` and returns the resulting vertex count (0 on
		// failure/empty), ready to render via the debugGpuIsosurfaceVertexCount path above. The
		// geometry stays GPU-resident (the compute shader's output SSBO doubles as the vertex
		// buffer) - only a 4-byte counter is read back, not the vertex data itself.
		[[nodiscard]] int RegenerateIsosurfaceGpu(const OrbitalGridData &grid, float isoValue);
		// Reads back the last-rendered frame for windowKey (must have been rendered via
		// RenderWindow this session) and writes it to a PNG. Returns false + fills error on
		// missing viewport or write failure. crop* are fractions (0..1) of width/height trimmed
		// from each edge before writing - a real pixel crop (changes output aspect ratio), not the
		// pan/zoom reframing that keeps the requested resolution's aspect intact.
		[[nodiscard]] bool CaptureWindowToPng(
			const std::string &windowKey,
			const Path &outputPath,
			std::string &error,
			float cropLeft = 0.0f,
			float cropRight = 0.0f,
			float cropTop = 0.0f,
			float cropBottom = 0.0f) const;

	private:
		Result<void> createStaticGeometry(const RendererPrimitiveMeshAssets &primitiveMeshes);
		void releaseStaticGeometry();
		Result<void> createSphereMesh(const RendererStaticMeshData &meshData);
		Result<void> createCylinderMesh(const RendererStaticMeshData &meshData);
		void createScreenGrid();
		void createIsosurfaceGeometry();
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
		void renderCellBox(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources);
		void renderGrid(
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			OpenGlViewportResources &resources,
			const RendererGlobalRenderSettings &globalSettings);
		void renderIsosurfaceOverlay(
			const std::vector<IsosurfaceVertex> &vertices,
			const RendererViewCamera &camera,
			const RendererGlobalRenderSettings &globalSettings);
		void renderIsosurfaceGpuOverlay(
			int vertexCount,
			const RendererViewCamera &camera,
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
		unsigned int m_IsosurfaceVao = 0;
		unsigned int m_IsosurfaceVbo = 0;
		unsigned int m_IsosurfaceGpuVao = 0;
		unsigned int m_IsosurfaceGridSsbo = 0;
		unsigned int m_IsosurfaceGpuVertexSsbo = 0;
		unsigned int m_IsosurfaceCounterSsbo = 0;
		unsigned int m_ComputeInputSsbo = 0;
		unsigned int m_ComputeOutputSsbo = 0;
		std::unordered_map<std::string, OpenGlViewportResources> m_Viewports;
	};
} // namespace DefectStudio
