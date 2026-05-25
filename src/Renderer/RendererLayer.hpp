#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Core/Layer.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class OpenGlRendererBackend;
	class RendererViewCamera;

	struct RendererAtomData
	{
		std::string element;
		glm::vec3 cartesianPosition = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::vec3 color = glm::vec3(0.7f, 0.7f, 0.7f);
		float radius = 0.35f;
		bool visible = true;
	};

	struct RendererColorGradient
	{
		glm::vec3 start = glm::vec3(0.6f, 0.6f, 0.6f);
		glm::vec3 finish = glm::vec3(0.9f, 0.9f, 0.9f);
	};

	struct RendererBondData
	{
		std::uint32_t firstAtomIndex = 0;
		std::uint32_t secondAtomIndex = 0;
		float radius = 0.09f;
		RendererColorGradient gradient;
	};

	struct RendererCellEdge
	{
		glm::vec3 start = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::vec3 finish = glm::vec3(0.0f, 0.0f, 0.0f);
	};

	struct RendererStructureData
	{
		std::string name;
		Path sourcePath;
		std::vector<RendererAtomData> atoms;
		std::vector<RendererBondData> bonds;
		std::vector<RendererCellEdge> cellEdges;
	};

	struct RendererWindowState
	{
		std::string title;
		RendererStructureData structure;
		Unique<RendererViewCamera> camera;
		ImVec2 viewportSize = ImVec2(640.0f, 480.0f);
		bool showGrid = true;
		bool showCellBox = true;
		bool showBonds = true;
		bool showAtoms = true;
	};

	struct RendererQuickTestRuntime
	{
		Path configDirectory;
		Path assetsDirectory;
		Path shaderDirectory;
		bool enableQuickTestingStartup = true;
	};

	class RendererLayer final : public Layer
	{
	public:
		explicit RendererLayer(RendererQuickTestRuntime runtime);
		~RendererLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		void OnImGuiRender() override;

	private:
		void loadQuickTestWindows();
		void loadCameraState();
		void saveCameraState() const;
		void renderStructureWindow(RendererWindowState &windowState, float deltaTime);
		void drawViewportToolbar(RendererWindowState &windowState);
		void drawViewportGizmo(RendererWindowState &windowState);
		Path rendererQuickStatePath() const;
		Path resolveShaderDirectory() const;

	private:
		RendererQuickTestRuntime m_Runtime;
		Unique<OpenGlRendererBackend> m_RendererBackend;
		std::vector<RendererWindowState> m_Windows;
		float m_LastDeltaTime = 0.0f;
		bool m_Attached = false;
	};
} // namespace DefectStudio
