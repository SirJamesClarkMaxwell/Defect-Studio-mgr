#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Memory.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	class OpenGlRendererBackend;
	class RendererViewCamera;
	class RendererPanel;
	class EventBus;
	struct ApplicationConfig;

	namespace AppEvents::Config
	{
		struct Applied;
	}

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
		glm::mat3 lattice = glm::mat3(1.0f);
		glm::mat3 reciprocalLattice = glm::mat3(1.0f);
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
		std::vector<std::size_t> selectedAtomIndices;
		float rotationStepDeg = 1.0f;
		float pixelStepPx = 10.0f;
		float percentStep = 10.0f;
		bool dragActive = false;
		ImVec2 lastMousePosition = ImVec2(0.0f, 0.0f);
		bool transitionActive = false;
		float transitionElapsed = 0.0f;
		float transitionDuration = 0.14f;
		glm::vec3 transitionStartTarget = glm::vec3(0.0f);
		glm::vec3 transitionEndTarget = glm::vec3(0.0f);
		float transitionStartDistance = 0.0f;
		float transitionEndDistance = 0.0f;
		float transitionStartYaw = 0.0f;
		float transitionEndYaw = 0.0f;
		float transitionStartPitch = 0.0f;
		float transitionEndPitch = 0.0f;
		float transitionStartRoll = 0.0f;
		float transitionEndRoll = 0.0f;
	};

	struct RendererLightingSettings
	{
		glm::vec3 keyDirection = glm::normalize(glm::vec3(0.6f, 0.8f, 0.5f));
		glm::vec3 fillDirection = glm::normalize(glm::vec3(-0.7f, 0.3f, 0.2f));
		glm::vec3 backDirection = glm::normalize(glm::vec3(0.0f, -0.4f, -0.8f));
		float ambientIntensity = 0.18f;
		float keyIntensity = 0.55f;
		float fillIntensity = 0.25f;
		float backIntensity = 0.12f;
		bool twoSided = true;
	};

	struct RendererViewportSettings
	{
		float axisButtonSize = 20.0f;
		float iconButtonSize = 18.0f;
	};

	struct RendererKeyboardShortcutSettings
	{
		ImGuiKey alignAxisA = ImGuiKey_A;
		ImGuiKey alignAxisB = ImGuiKey_B;
		ImGuiKey alignAxisC = ImGuiKey_C;
		ImGuiKey orbitLeft = ImGuiKey_LeftArrow;
		ImGuiKey orbitRight = ImGuiKey_RightArrow;
		ImGuiKey orbitUp = ImGuiKey_UpArrow;
		ImGuiKey orbitDown = ImGuiKey_DownArrow;
		ImGuiKey rollLeft = ImGuiKey_Q;
		ImGuiKey rollRight = ImGuiKey_E;
		ImGuiKey zoomIn = ImGuiKey_R;
		ImGuiKey zoomOut = ImGuiKey_F;
		ImGuiKey focusSelectedAtom = ImGuiKey_Period;
	};

	struct RendererGlobalRenderSettings
	{
		glm::vec4 backgroundColor = glm::vec4(0.06f, 0.07f, 0.08f, 1.0f);
		float orbitSensitivity = 1.0f;
		float panSensitivity = 1.0f;
		float zoomSensitivity = 1.0f;
		float focusSelectedAtomDistance = 3.0f;
		float focusSelectedAtomTransitionSeconds = 0.18f;
		bool focusSelectedAtomRespectAtomRadius = true;
		float focusSelectedAtomRadiusMultiplier = 2.0f;
		bool invertZoom = false;
		bool touchpadNavigation = true;
		CameraProjection defaultCameraProjection = CameraProjection::Perspective;
		RendererLightingSettings lighting;
		RendererViewportSettings viewport;
		RendererKeyboardShortcutSettings shortcuts;
	};

	struct RendererToolbarIconTexture
	{
		unsigned int rendererId = 0;
		int width = 0;
		int height = 0;
		bool loadAttempted = false;
	};

	struct RendererQuickTestRuntime
	{
		Path configDirectory;
		Path assetsDirectory;
		Path shaderDirectory;
		Ref<EventBus> eventBus;
		bool enableQuickTestingStartup = true;
	};

	class RendererLayer final : public Layer, public EventReceiver
	{
	public:
		explicit RendererLayer(RendererQuickTestRuntime runtime);
		~RendererLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		void OnImGuiRender() override;
		void ApplyConfig(const ApplicationConfig &config);

	private:
		void loadQuickTestWindows();
		void bindConfigEvents();
		void onConfigApplied(const AppEvents::Config::Applied &event);
		void applyDefaultProjectionToWindows();
		const RendererToolbarIconTexture *getToolbarIcon(const std::string &iconFileName);
		void releaseToolbarIcons();
		Path resolveShaderDirectory() const;

	private:
		RendererQuickTestRuntime m_Runtime;
		Unique<OpenGlRendererBackend> m_RendererBackend;
		std::vector<RendererWindowState> m_Windows;
		RendererGlobalRenderSettings m_GlobalRenderSettings;
		Ref<EventBus> m_EventBus;
		Unique<RendererPanel> m_Panel;
		std::unordered_map<std::string, RendererToolbarIconTexture> m_ToolbarIcons;
		std::string m_SelectedPeriodicElement = "C";
		bool m_ShowPeriodicTableWindow = true;
		float m_LastDeltaTime = 0.0f;
		bool m_Attached = false;

		friend class RendererPanel;
	};
} // namespace DefectStudio
