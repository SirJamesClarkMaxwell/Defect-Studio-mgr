#pragma once

#include <unordered_map>

#include <imgui.h>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/OpenGl/OpenGlFrameBuffer.hpp"
#include "Renderer/OpenGl/OpenGlShaderLibrary.hpp"

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

	struct OpenGlViewportResources
	{
		OpenGlFrameBuffer frameBuffer;
		Time::SteadyTimePoint lastRenderTime{};
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

	class RendererViewCamera;

	class OpenGlRendererBackend
	{
	public:
		OpenGlRendererBackend() = default;
		~OpenGlRendererBackend();

		Result<void> Initialize(const Path &shaderDirectory);
		void Shutdown();
		void ReloadShadersIfNeeded();
		[[nodiscard]] unsigned int RenderWindow(
			const std::string &windowKey,
			const RendererStructureData &structure,
			const RendererViewCamera &camera,
			int viewportWidth,
			int viewportHeight,
			bool showAtoms,
			bool showBonds,
			bool showCellBox,
			bool showGrid);

	private:
		void createStaticGeometry();
		void releaseStaticGeometry();
		void createSphereMesh();
		void createCylinderMesh();
		void createScreenGrid();
		void configureOpenGlState() const;
		void renderAtoms(const RendererStructureData &structure, const RendererViewCamera &camera);
		void renderBonds(const RendererStructureData &structure, const RendererViewCamera &camera);
		void renderCellBox(const RendererStructureData &structure, const RendererViewCamera &camera);
		void renderGrid(const RendererViewCamera &camera);
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

