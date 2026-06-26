#include "Core/dspch.hpp"

#include "Renderer/RendererLayer.hpp"

#include "App/ApplicationState.hpp"
#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Assert.hpp"
#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glad/gl.h>
#include <stb_image.h>
#include <yaml-cpp/yaml.h>

#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Time.hpp"
#include "IO/TextFileIO.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kMinSensitivity = 0.05f;
		constexpr float kMaxSensitivity = 4.0f;
		constexpr float kMinRotationSpeed = 0.1f;
		constexpr float kMaxRotationSpeed = 10.0f;
		constexpr float kMinFocusDistance = 0.25f;
		constexpr float kMaxFocusDistance = 256.0f;
		constexpr float kMinFocusTransitionSeconds = 0.02f;
		constexpr float kMaxFocusTransitionSeconds = 3.0f;
		constexpr float kMinFocusRadiusMultiplier = 0.1f;
		constexpr float kMaxFocusRadiusMultiplier = 32.0f;
		constexpr float kMinWheelStepDelta = 0.1f;
		constexpr float kMaxWheelStepDelta = 45.0f;
		constexpr float kMinGridPaddingPercent = 0.0f;
		constexpr float kMaxGridPaddingPercent = 400.0f;
		constexpr float kMinGridSpacing = 0.05f;
		constexpr float kMaxGridSpacing = 25.0f;
		constexpr float kMinGridPlaneZ = -10000.0f;
		constexpr float kMaxGridPlaneZ = 10000.0f;

		[[nodiscard]] Path BuildShaderDirectoryFromCurrentPath()
		{
			return Path::FromResolved(
				FileSystem::CurrentPath()
				/ "src"
				/ "Renderer"
				/ "OpenGl"
				/ "Shaders");
		}

		[[nodiscard]] Path BuildShaderDirectoryFromAssetsRoot(const Path &assetsDirectory)
		{
			if (assetsDirectory.Empty())
				return {};

			// Dev fallback
			const FilePath repositoryRoot = assetsDirectory.Native()
				.parent_path()
				.parent_path()
				.parent_path();
			if (repositoryRoot.empty())
				return {};

			return Path::FromResolved(
				repositoryRoot
				/ "src"
				/ "Renderer"
				/ "OpenGl"
				/ "Shaders");
		}

		[[nodiscard]] const char *ProjectionToString(CameraProjection projection)
		{
			return projection == CameraProjection::Orthographic ? "orthographic" : "perspective";
		}

		[[nodiscard]] CameraProjection ProjectionFromString(const std::string &value)
		{
			if (value == "orthographic" || value == "ORTHO" || value == "ortho")
				return CameraProjection::Orthographic;
			return CameraProjection::Perspective;
		}

		float ComputeCameraTransitionDurationSeconds(float rotationSpeed)
		{
			const float safeSpeed = std::max(0.1f, rotationSpeed);
			return std::clamp(0.14f / safeSpeed, 0.02f, 0.50f);
		}

		[[nodiscard]] std::string ToUpperAscii(std::string_view text)
		{
			std::string upper;
			upper.reserve(text.size());
			for (const char character : text)
			{
				const unsigned char unsignedCharacter = static_cast<unsigned char>(character);
				upper.push_back(static_cast<char>(std::toupper(unsignedCharacter)));
			}
			return upper;
		}

		[[nodiscard]] ImGuiKey ParseRendererShortcutKey(std::string_view token, ImGuiKey fallback)
		{
			const std::string normalized = ToUpperAscii(token);
			if (normalized.empty() || normalized == "NONE")
				return ImGuiKey_None;

			if (normalized == "A")
				return ImGuiKey_A;
			if (normalized == "B")
				return ImGuiKey_B;
			if (normalized == "C")
				return ImGuiKey_C;
			if (normalized == "D")
				return ImGuiKey_D;
			if (normalized == "E")
				return ImGuiKey_E;
			if (normalized == "F")
				return ImGuiKey_F;
			if (normalized == "Q")
				return ImGuiKey_Q;
			if (normalized == "R")
				return ImGuiKey_R;
			if (normalized == "S")
				return ImGuiKey_S;
			if (normalized == "W")
				return ImGuiKey_W;
			if (normalized == "LEFT" || normalized == "LEFTARROW")
				return ImGuiKey_LeftArrow;
			if (normalized == "RIGHT" || normalized == "RIGHTARROW")
				return ImGuiKey_RightArrow;
			if (normalized == "UP" || normalized == "UPARROW")
				return ImGuiKey_UpArrow;
			if (normalized == "DOWN" || normalized == "DOWNARROW")
				return ImGuiKey_DownArrow;
			if (normalized == "PERIOD" || normalized == ".")
				return ImGuiKey_Period;
			if (normalized == "COMMA" || normalized == ",")
				return ImGuiKey_Comma;
			if (normalized == "MINUS" || normalized == "-")
				return ImGuiKey_Minus;
			if (normalized == "EQUAL" || normalized == "=" || normalized == "PLUS")
				return ImGuiKey_Equal;
			if (normalized == "SPACE")
				return ImGuiKey_Space;

			return fallback;
		}

		[[nodiscard]] bool ParseSymbolSequence(
			const YAML::Node &node,
			std::vector<std::string> &outSymbols)
		{
			if (!node || !node.IsSequence())
				return false;

			outSymbols.clear();
			for (const YAML::Node &entry : node)
			{
				std::string symbol;
				if (entry.IsMap())
					symbol = entry["symbol"].as<std::string>("");
				else if (entry.IsScalar())
					symbol = entry.as<std::string>("");

				if (!symbol.empty())
					outSymbols.push_back(std::move(symbol));
			}

			return !outSymbols.empty();
		}

		void LoadPeriodicTableFromConfig(
			const Path &path,
			std::vector<std::string> &outElements,
			std::vector<std::string> &outLanthanides,
			std::vector<std::string> &outActinides)
		{
			std::string yamlText;
			std::string error;
			if (!TextFileIO::Load(path, yamlText, error))
			{
				DS_LOG_ERROR(
					"Periodic table config load failed: {} | {}",
					path.String(),
					error);
				return;
			}

			try
			{
				YAML::Node root = YAML::Load(yamlText);
				if (!root || !root.IsMap())
				{
					DS_LOG_ERROR("Periodic table YAML root is not a map: {}", path.String());
					return;
				}

				if (!ParseSymbolSequence(root["elements"], outElements))
					DS_LOG_ERROR("Periodic table YAML has no valid 'elements' sequence: {}", path.String());
				if (!ParseSymbolSequence(root["lanthanides"], outLanthanides))
					DS_LOG_ERROR("Periodic table YAML has no valid 'lanthanides' sequence: {}", path.String());
				if (!ParseSymbolSequence(root["actinides"], outActinides))
					DS_LOG_ERROR("Periodic table YAML has no valid 'actinides' sequence: {}", path.String());
			}
			catch (const std::exception &exception)
			{
				DS_LOG_ERROR(
					"Periodic table YAML parse failed: {} | {}",
					path.String(),
					exception.what());
			}
		}

	}

	RendererLayer::RendererLayer(RendererStartupConfig startupConfig)
		: Layer("RendererLayer"), m_StartupConfig(std::move(startupConfig))
	{
		m_Panel = CreateUnique<RendererPanel>(*this);
	}

	RendererLayer::~RendererLayer() = default;

	void RendererLayer::BindEventBus(Ref<EventBus> eventBus)
	{
		DS_ASSERT(!m_Attached, "BindEventBus must be called before OnAttach");
		m_EventBus = std::move(eventBus);
	}

	Ref<EventBus> RendererLayer::GetEventBus() const
	{
		return m_EventBus;
	}

	std::vector<RendererWindowState> &RendererLayer::GetWindows()
	{
		return m_Windows;
	}

	const std::vector<RendererWindowState> &RendererLayer::GetWindows() const
	{
		return m_Windows;
	}

	RendererGlobalRenderSettings &RendererLayer::GetGlobalSettings()
	{
		return m_GlobalRenderSettings;
	}

	const RendererGlobalRenderSettings &RendererLayer::GetGlobalSettings() const
	{
		return m_GlobalRenderSettings;
	}

	bool RendererLayer::IsAttached() const noexcept
	{
		return m_Attached;
	}

	const RendererToolbarIconTexture *RendererLayer::GetToolbarIcon(const std::string &fileName) const
	{
		return getToolbarIcon(fileName);
	}

	const std::vector<std::string> &RendererLayer::GetPeriodicTableSymbols() const
	{
		return m_PeriodicTableSymbols;
	}

	const std::vector<std::string> &RendererLayer::GetLanthanideSymbols() const
	{
		return m_LanthanideSymbols;
	}

	const std::vector<std::string> &RendererLayer::GetActinideSymbols() const
	{
		return m_ActinideSymbols;
	}

	unsigned int RendererLayer::RenderToFbo(
		const std::string &windowKey,
		const RendererStructureData &structure,
		const RendererWindowState &windowState,
		const RendererGlobalRenderSettings &settings)
	{
		if (m_RendererBackend == nullptr || windowState.camera == nullptr)
			return 0;

		return m_RendererBackend->RenderWindow(
			windowKey,
			structure,
			*windowState.camera,
			settings,
			static_cast<int>(windowState.viewportSize.x),
			static_cast<int>(windowState.viewportSize.y),
			windowState.showAtoms,
			windowState.showBonds,
			windowState.showCellBox,
			windowState.showGrid,
			windowState.selectedAtomIndices);
	}

	void RendererLayer::CollectProfilingData()
	{
		if (m_RendererBackend != nullptr)
			m_RendererBackend->CollectProfilingData();
	}

	bool &RendererLayer::GetShowPeriodicTableWindow()
	{
		return m_ShowPeriodicTableWindow;
	}

	std::string &RendererLayer::GetSelectedPeriodicElement()
	{
		return m_SelectedPeriodicElement;
	}

	void RendererLayer::BeginViewInteraction(const std::string &windowId, std::string sourceAction)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr || windowState->camera == nullptr || windowState->viewInteractionActive)
			return;

		windowState->viewInteractionActive = true;
		windowState->viewInteractionSource = !sourceAction.empty()
			? std::move(sourceAction)
			: "view.change";
		windowState->viewInteractionStart = captureViewSnapshot(*windowState);
	}

	void RendererLayer::CommitViewInteraction(const std::string &windowId)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr || !windowState->viewInteractionActive)
			return;

		const RendererViewSnapshot before = windowState->viewInteractionStart;
		const RendererViewSnapshot after = captureViewSnapshot(*windowState);
		const std::string source = windowState->viewInteractionSource;
		windowState->viewInteractionActive = false;
		windowState->viewInteractionSource.clear();
		pushViewChange(*windowState, before, after, source.c_str());
	}

	void RendererLayer::CancelViewInteraction(const std::string &windowId)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr)
			return;

		windowState->viewInteractionActive = false;
		windowState->viewInteractionSource.clear();
	}

	void RendererLayer::UndoViewChange(const std::string &windowId)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr)
			return;

		if (windowState->viewInteractionActive)
			CommitViewInteraction(windowId);
		if (windowState->viewUndoHistory.empty())
			return;

		RendererViewStateChange change = std::move(windowState->viewUndoHistory.back());
		windowState->viewUndoHistory.pop_back();
		restoreViewSnapshot(*windowState, change.before, "view.undo");
		windowState->viewRedoHistory.push_back(std::move(change));
	}

	void RendererLayer::RedoViewChange(const std::string &windowId)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr)
			return;

		if (windowState->viewInteractionActive)
			CancelViewInteraction(windowId);
		if (windowState->viewRedoHistory.empty())
			return;

		RendererViewStateChange change = std::move(windowState->viewRedoHistory.back());
		windowState->viewRedoHistory.pop_back();
		restoreViewSnapshot(*windowState, change.after, "view.redo");
		windowState->viewUndoHistory.push_back(std::move(change));
	}

	void RendererLayer::OnAttach()
	{
		m_RendererBackend = CreateUnique<OpenGlRendererBackend>();
		const Path shaderDirectory = resolveShaderDirectory();
		if (shaderDirectory.Empty() || !FileSystem::Exists(shaderDirectory.Native()))
		{
			DS_LOG_ERROR(
				"RendererLayer initialization failed: shader directory not found [{}]",
				shaderDirectory.String());
			m_RendererBackend.reset();
			return;
		}

		Result<void> initializeResult = m_RendererBackend->Initialize(
			shaderDirectory,
			m_StartupConfig.primitiveMeshes);
		if (!initializeResult.HasValue())
		{
			DS_LOG_ERROR(
				"RendererLayer initialization failed: {}",
				initializeResult.Error().technicalDetails);
			m_RendererBackend.reset();
			return;
		}

		const Path periodicTablePath = m_StartupConfig.assetsDirectory.Native()
			/ "config"
			/ "periodic_table.yaml";
		LoadPeriodicTableFromConfig(
			periodicTablePath,
			m_PeriodicTableSymbols,
			m_LanthanideSymbols,
			m_ActinideSymbols);

		if (m_StartupConfig.loadDefaultScene)
			loadDefaultWindows();
		else
			m_Windows.clear();
		applyDefaultProjectionToWindows();
		if (m_EventBus != nullptr)
		{
			bindConfigEvents();
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::OrbitDelta>(
				std::bind_front(&RendererLayer::onOrbitDelta, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::PanDelta>(
				std::bind_front(&RendererLayer::onPanDelta, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::ZoomDelta>(
				std::bind_front(&RendererLayer::onZoomDelta, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::FocusChanged>(
				std::bind_front(&RendererLayer::onViewportFocusChanged, this)));
		}
		m_Attached = true;
		DS_LOG_INFO("Renderer shader root: {}", shaderDirectory.String());
		DS_LOG_INFO("RendererLayer attached with {} quick-test windows", m_Windows.size());
	}

	void RendererLayer::OnDetach()
	{
		releaseToolbarIcons();
		ClearSubscriptions();
		if (m_RendererBackend != nullptr)
			m_RendererBackend->Shutdown();
		m_RendererBackend.reset();
		m_Windows.clear();
		m_Attached = false;
		DS_LOG_INFO("RendererLayer detached");
	}

	void RendererLayer::OnUpdate(float deltaTime)
	{
		m_LastDeltaTime = deltaTime;
		if (m_RendererBackend != nullptr)
			m_RendererBackend->ReloadShadersIfNeeded();
	}

	void RendererLayer::OnImGuiRender()
	{
		if (!m_Attached || m_RendererBackend == nullptr || m_Panel == nullptr)
			return;

		m_Panel->Render(m_LastDeltaTime);
	}

	void RendererLayer::ApplyConfig(const ApplicationConfig &config)
	{
		const RendererGridSettings previousGridSettings = m_GlobalRenderSettings.grid;

		m_GlobalRenderSettings.backgroundColor = glm::vec4(
			config.renderer.backgroundColor[0],
			config.renderer.backgroundColor[1],
			config.renderer.backgroundColor[2],
			config.renderer.backgroundColor[3]);
		m_GlobalRenderSettings.orbitSensitivity = config.renderer.orbitSensitivity;
		m_GlobalRenderSettings.panSensitivity = config.renderer.panSensitivity;
		m_GlobalRenderSettings.zoomSensitivity = config.renderer.zoomSensitivity;
		m_GlobalRenderSettings.rotationSpeed = config.renderer.rotationSpeed;
		m_GlobalRenderSettings.focusSelectedAtomDistance = config.renderer.focusSelectedAtomDistance;
		m_GlobalRenderSettings.focusSelectedAtomTransitionSeconds = config.renderer.focusSelectedAtomTransitionSeconds;
		m_GlobalRenderSettings.focusSelectedAtomRespectAtomRadius = config.renderer.focusSelectedAtomRespectAtomRadius;
		m_GlobalRenderSettings.focusSelectedAtomRadiusMultiplier = config.renderer.focusSelectedAtomRadiusMultiplier;
		m_GlobalRenderSettings.invertZoom = config.renderer.invertZoom;
		m_GlobalRenderSettings.touchpadNavigation = config.renderer.touchpadNavigation;
		m_GlobalRenderSettings.defaultCameraProjection = ProjectionFromString(config.renderer.defaultProjection);
		m_GlobalRenderSettings.lighting.ambientIntensity = config.renderer.lighting.ambientIntensity;
		m_GlobalRenderSettings.lighting.keyIntensity = config.renderer.lighting.keyIntensity;
		m_GlobalRenderSettings.lighting.fillIntensity = config.renderer.lighting.fillIntensity;
		m_GlobalRenderSettings.lighting.backIntensity = config.renderer.lighting.backIntensity;
		m_GlobalRenderSettings.lighting.twoSided = config.renderer.lighting.twoSided;
		m_GlobalRenderSettings.lighting.keyDirection = glm::vec3(
			config.renderer.lighting.keyDirection[0],
			config.renderer.lighting.keyDirection[1],
			config.renderer.lighting.keyDirection[2]);
		m_GlobalRenderSettings.lighting.fillDirection = glm::vec3(
			config.renderer.lighting.fillDirection[0],
			config.renderer.lighting.fillDirection[1],
			config.renderer.lighting.fillDirection[2]);
		m_GlobalRenderSettings.lighting.backDirection = glm::vec3(
			config.renderer.lighting.backDirection[0],
			config.renderer.lighting.backDirection[1],
			config.renderer.lighting.backDirection[2]);
		m_GlobalRenderSettings.viewport.axisButtonSize = std::clamp(
			config.renderer.viewport.axisButtonSize,
			10.0f,
			48.0f);
		m_GlobalRenderSettings.viewport.iconButtonSize = std::clamp(
			config.renderer.viewport.iconButtonSize,
			10.0f,
			48.0f);
		m_GlobalRenderSettings.toolbarWheel.rotationStepDelta = config.renderer.toolbarWheel.rotationStepDelta;
		m_GlobalRenderSettings.toolbarWheel.zoomStepDelta = config.renderer.toolbarWheel.zoomStepDelta;
		m_GlobalRenderSettings.toolbarWheel.ctrlPresetValues = config.renderer.toolbarWheel.ctrlPresetValues;
		m_GlobalRenderSettings.grid.autoFitToStructureBounds = config.renderer.grid.autoFitToStructureBounds;
		m_GlobalRenderSettings.grid.paddingPercent = config.renderer.grid.paddingPercent;
		m_GlobalRenderSettings.grid.spacing = config.renderer.grid.spacing;
		m_GlobalRenderSettings.grid.planeZ = config.renderer.grid.planeZ;
		m_GlobalRenderSettings.shortcuts.alignAxisA = ParseRendererShortcutKey(
			config.renderer.shortcuts.alignAxisA,
			ImGuiKey_A);
		m_GlobalRenderSettings.shortcuts.alignAxisB = ParseRendererShortcutKey(
			config.renderer.shortcuts.alignAxisB,
			ImGuiKey_B);
		m_GlobalRenderSettings.shortcuts.alignAxisC = ParseRendererShortcutKey(
			config.renderer.shortcuts.alignAxisC,
			ImGuiKey_C);
		m_GlobalRenderSettings.shortcuts.orbitLeft = ParseRendererShortcutKey(
			config.renderer.shortcuts.orbitLeft,
			ImGuiKey_LeftArrow);
		m_GlobalRenderSettings.shortcuts.orbitRight = ParseRendererShortcutKey(
			config.renderer.shortcuts.orbitRight,
			ImGuiKey_RightArrow);
		m_GlobalRenderSettings.shortcuts.orbitUp = ParseRendererShortcutKey(
			config.renderer.shortcuts.orbitUp,
			ImGuiKey_UpArrow);
		m_GlobalRenderSettings.shortcuts.orbitDown = ParseRendererShortcutKey(
			config.renderer.shortcuts.orbitDown,
			ImGuiKey_DownArrow);
		m_GlobalRenderSettings.shortcuts.rollLeft = ParseRendererShortcutKey(
			config.renderer.shortcuts.rollLeft,
			ImGuiKey_Q);
		m_GlobalRenderSettings.shortcuts.rollRight = ParseRendererShortcutKey(
			config.renderer.shortcuts.rollRight,
			ImGuiKey_E);
		m_GlobalRenderSettings.shortcuts.zoomIn = ParseRendererShortcutKey(
			config.renderer.shortcuts.zoomIn,
			ImGuiKey_R);
		m_GlobalRenderSettings.shortcuts.zoomOut = ParseRendererShortcutKey(
			config.renderer.shortcuts.zoomOut,
			ImGuiKey_F);
		m_GlobalRenderSettings.shortcuts.focusSelectedAtom = ParseRendererShortcutKey(
			config.renderer.shortcuts.focusSelectedAtom,
			ImGuiKey_Period);

		if (glm::length(m_GlobalRenderSettings.lighting.keyDirection) <= 0.001f)
			m_GlobalRenderSettings.lighting.keyDirection = glm::normalize(glm::vec3(0.6f, 0.8f, 0.5f));
		if (glm::length(m_GlobalRenderSettings.lighting.fillDirection) <= 0.001f)
			m_GlobalRenderSettings.lighting.fillDirection = glm::normalize(glm::vec3(-0.7f, 0.3f, 0.2f));
		if (glm::length(m_GlobalRenderSettings.lighting.backDirection) <= 0.001f)
			m_GlobalRenderSettings.lighting.backDirection = glm::normalize(glm::vec3(0.0f, -0.4f, -0.8f));

		m_GlobalRenderSettings.orbitSensitivity = std::clamp(m_GlobalRenderSettings.orbitSensitivity, kMinSensitivity, kMaxSensitivity);
		m_GlobalRenderSettings.panSensitivity = std::clamp(m_GlobalRenderSettings.panSensitivity, kMinSensitivity, kMaxSensitivity);
		m_GlobalRenderSettings.zoomSensitivity = std::clamp(m_GlobalRenderSettings.zoomSensitivity, kMinSensitivity, kMaxSensitivity);
		m_GlobalRenderSettings.rotationSpeed = std::clamp(
			m_GlobalRenderSettings.rotationSpeed,
			kMinRotationSpeed,
			kMaxRotationSpeed);
		m_GlobalRenderSettings.focusSelectedAtomDistance = std::clamp(
			m_GlobalRenderSettings.focusSelectedAtomDistance,
			kMinFocusDistance,
			kMaxFocusDistance);
		m_GlobalRenderSettings.focusSelectedAtomTransitionSeconds = std::clamp(
			m_GlobalRenderSettings.focusSelectedAtomTransitionSeconds,
			kMinFocusTransitionSeconds,
			kMaxFocusTransitionSeconds);
		m_GlobalRenderSettings.focusSelectedAtomRadiusMultiplier = std::clamp(
			m_GlobalRenderSettings.focusSelectedAtomRadiusMultiplier,
			kMinFocusRadiusMultiplier,
			kMaxFocusRadiusMultiplier);
		m_GlobalRenderSettings.toolbarWheel.rotationStepDelta = std::clamp(
			m_GlobalRenderSettings.toolbarWheel.rotationStepDelta,
			kMinWheelStepDelta,
			kMaxWheelStepDelta);
		m_GlobalRenderSettings.toolbarWheel.zoomStepDelta = std::clamp(
			m_GlobalRenderSettings.toolbarWheel.zoomStepDelta,
			kMinWheelStepDelta,
			kMaxWheelStepDelta);
		std::vector<float> sanitizedPresets;
		sanitizedPresets.reserve(m_GlobalRenderSettings.toolbarWheel.ctrlPresetValues.size());
		for (const float value : m_GlobalRenderSettings.toolbarWheel.ctrlPresetValues)
		{
			if (!std::isfinite(value))
				continue;
			sanitizedPresets.push_back(std::clamp(value, 0.0f, 180.0f));
		}
		if (sanitizedPresets.empty())
		{
			sanitizedPresets = {
				0.0f,
				1.0f,
				3.0f,
				5.0f,
				10.0f,
				15.0f,
				30.0f,
				45.0f,
				60.0f,
				90.0f,
				180.0f};
		}
		std::sort(sanitizedPresets.begin(), sanitizedPresets.end());
		const auto uniqueEnd = std::unique(sanitizedPresets.begin(), sanitizedPresets.end(), [](float a, float b) {
			return std::abs(a - b) <= 0.0001f;
		});
		sanitizedPresets.erase(uniqueEnd, sanitizedPresets.end());
		m_GlobalRenderSettings.toolbarWheel.ctrlPresetValues = std::move(sanitizedPresets);
		m_GlobalRenderSettings.grid.paddingPercent = std::clamp(
			m_GlobalRenderSettings.grid.paddingPercent,
			kMinGridPaddingPercent,
			kMaxGridPaddingPercent);
		m_GlobalRenderSettings.grid.spacing = std::clamp(
			m_GlobalRenderSettings.grid.spacing,
			kMinGridSpacing,
			kMaxGridSpacing);
		m_GlobalRenderSettings.grid.planeZ = std::clamp(
			m_GlobalRenderSettings.grid.planeZ,
			kMinGridPlaneZ,
			kMaxGridPlaneZ);

		const bool gridSettingsChanged =
			previousGridSettings.autoFitToStructureBounds != m_GlobalRenderSettings.grid.autoFitToStructureBounds ||
			std::abs(previousGridSettings.paddingPercent - m_GlobalRenderSettings.grid.paddingPercent) > 0.0001f ||
			std::abs(previousGridSettings.spacing - m_GlobalRenderSettings.grid.spacing) > 0.0001f ||
			std::abs(previousGridSettings.planeZ - m_GlobalRenderSettings.grid.planeZ) > 0.0001f;
		if (gridSettingsChanged && m_RendererBackend != nullptr)
			m_RendererBackend->MarkGridDirty();

		applyDefaultProjectionToWindows();
	}

	void RendererLayer::loadDefaultWindows()
	{
		DS_LOG_INFO("Renderer startup default scene uses C++ POSCAR parser; Python bridge available for on-demand loading");
		m_Windows = BuildRendererStartupWindows(
			m_StartupConfig.startupLayout.windows,
			m_StartupConfig.atomStyleTable,
			m_StartupConfig.elementPropertiesTable);
		if (m_RendererBackend != nullptr)
			m_RendererBackend->MarkGridDirty();
	}


	void RendererLayer::bindConfigEvents()
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(m_EventBus->Subscribe<AppEvents::Config::Applied>(
			[this](const AppEvents::Config::Applied &event) { onConfigApplied(event); }));
	}

	RendererWindowState *RendererLayer::findWindowById(const std::string &windowId)
	{
		for (RendererWindowState &windowState : m_Windows)
		{
			if (windowState.windowId == windowId)
				return &windowState;
		}
		return nullptr;
	}

	RendererViewSnapshot RendererLayer::captureViewSnapshot(const RendererWindowState &windowState) const
	{
		RendererViewSnapshot snapshot;
		if (windowState.camera == nullptr)
			return snapshot;

		snapshot.target = windowState.camera->Target();
		snapshot.distance = windowState.camera->Distance();
		snapshot.yaw = windowState.camera->Yaw();
		snapshot.pitch = windowState.camera->Pitch();
		snapshot.roll = windowState.camera->Roll();
		snapshot.projection = windowState.camera->Projection();
		return snapshot;
	}

	void RendererLayer::restoreViewSnapshot(
		RendererWindowState &windowState,
		const RendererViewSnapshot &snapshot,
		const char *sourceAction)
	{
		if (windowState.camera == nullptr)
			return;

		const char *resolvedSourceAction =
			(sourceAction != nullptr && sourceAction[0] != '\0')
				? sourceAction
				: "view.restore";
		const float targetDistance = std::max(snapshot.distance, 0.1f);
		const float startYaw = windowState.camera->Yaw();
		const float startPitch = windowState.camera->Pitch();
		const float startRoll = windowState.camera->Roll();
		const glm::quat startOrientation = RendererViewCamera::CameraOrientationQuatFromEuler(startYaw, startPitch, startRoll);
		glm::quat endOrientation = RendererViewCamera::CameraOrientationQuatFromEuler(snapshot.yaw, snapshot.pitch, snapshot.roll);
		if (glm::dot(startOrientation, endOrientation) < 0.0f)
			endOrientation = -endOrientation;

		windowState.camera->SetProjection(snapshot.projection);
		windowState.transitionActive = true;
		windowState.transitionElapsed = 0.0f;
		windowState.transitionDuration = ComputeCameraTransitionDurationSeconds(
			m_GlobalRenderSettings.rotationSpeed);
		windowState.transitionStartTarget = windowState.camera->Target();
		windowState.transitionEndTarget = snapshot.target;
		windowState.transitionStartDistance = windowState.camera->Distance();
		windowState.transitionEndDistance = targetDistance;
		windowState.transitionStartYaw = startYaw;
		windowState.transitionEndYaw = snapshot.yaw;
		windowState.transitionStartPitch = startPitch;
		windowState.transitionEndPitch = snapshot.pitch;
		windowState.transitionStartRoll = startRoll;
		windowState.transitionEndRoll = snapshot.roll;
		windowState.transitionStartOrientation = startOrientation;
		windowState.transitionEndOrientation = endOrientation;
		windowState.transitionSourceAction = resolvedSourceAction;
		DS_LOG_DEBUG("Renderer view restore transition source={}", resolvedSourceAction);
	}

	void RendererLayer::pushViewChange(
		RendererWindowState &windowState,
		const RendererViewSnapshot &before,
		const RendererViewSnapshot &after,
		const char *sourceAction)
	{
		constexpr float kEpsilon = 0.0001f;
		const bool sameTarget = glm::length(before.target - after.target) <= kEpsilon;
		const bool sameScalars =
			std::abs(before.distance - after.distance) <= kEpsilon &&
			std::abs(RendererViewCamera::NormalizeAngleRadians(before.yaw - after.yaw)) <= kEpsilon &&
			std::abs(RendererViewCamera::NormalizeAngleRadians(before.pitch - after.pitch)) <= kEpsilon &&
			std::abs(RendererViewCamera::NormalizeAngleRadians(before.roll - after.roll)) <= kEpsilon &&
			before.projection == after.projection;
		if (sameTarget && sameScalars)
			return;

		RendererViewStateChange change;
		change.description = sourceAction != nullptr ? sourceAction : "view.change";
		change.before = before;
		change.after = after;
		windowState.viewUndoHistory.push_back(std::move(change));
		constexpr std::size_t kMaxViewHistoryEntries = 256u;
		if (windowState.viewUndoHistory.size() > kMaxViewHistoryEntries)
			windowState.viewUndoHistory.erase(windowState.viewUndoHistory.begin());
		windowState.viewRedoHistory.clear();
	}

	void RendererLayer::onOrbitDelta(const RendererEvents::Viewport::OrbitDelta &event)
	{
		RendererWindowState *windowState = findWindowById(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;
		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		windowState->camera->Orbit(event.dx, event.dy);
		const RendererViewSnapshot after = captureViewSnapshot(*windowState);
		if (!windowState->viewInteractionActive)
			pushViewChange(*windowState, before, after, "event.orbit");
	}

	void RendererLayer::onPanDelta(const RendererEvents::Viewport::PanDelta &event)
	{
		RendererWindowState *windowState = findWindowById(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;
		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		windowState->camera->Pan(event.dx, event.dy);
		const RendererViewSnapshot after = captureViewSnapshot(*windowState);
		if (!windowState->viewInteractionActive)
			pushViewChange(*windowState, before, after, "event.pan");
	}

	void RendererLayer::onZoomDelta(const RendererEvents::Viewport::ZoomDelta &event)
	{
		RendererWindowState *windowState = findWindowById(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;
		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		windowState->camera->Zoom(event.amount);
		const RendererViewSnapshot after = captureViewSnapshot(*windowState);
		if (!windowState->viewInteractionActive)
			pushViewChange(*windowState, before, after, "event.zoom");
	}

	void RendererLayer::onViewportFocusChanged(const RendererEvents::Viewport::FocusChanged &event)
	{
		DS_LOG_TRACE("Renderer viewport '{}' focus: {}", event.windowId, event.focused);
	}

	void RendererLayer::onConfigApplied(const AppEvents::Config::Applied &event)
	{
		ApplyConfig(event.config);
	}

	void RendererLayer::applyDefaultProjectionToWindows()
	{
		for (RendererWindowState &windowState : m_Windows)
		{
			if (windowState.camera == nullptr)
				continue;
			windowState.camera->SetProjection(m_GlobalRenderSettings.defaultCameraProjection);
		}
	}

	const RendererToolbarIconTexture *RendererLayer::getToolbarIcon(const std::string &iconFileName) const
	{
		if (iconFileName.empty())
			return nullptr;

		RendererToolbarIconTexture &icon = m_ToolbarIcons[iconFileName];
		if (icon.loadAttempted)
			return icon.rendererId != 0 ? &icon : nullptr;

		icon.loadAttempted = true;

		const Path iconPath = m_StartupConfig.assetsDirectory / Path("icons") / Path(iconFileName);
		if (!FileSystem::Exists(iconPath.Native()))
		{
			DS_LOG_WARN("Renderer toolbar icon missing: {}", iconPath.String());
			return nullptr;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc *pixels = stbi_load(iconPath.String().c_str(), &width, &height, &channels, 4);
		if (pixels == nullptr || width <= 0 || height <= 0)
		{
			const char *reason = stbi_failure_reason();
			DS_LOG_WARN(
				"Renderer toolbar icon load failed [{}]: {}",
				iconPath.String(),
				reason != nullptr ? reason : "unknown error");
			if (pixels != nullptr)
				stbi_image_free(pixels);
			return nullptr;
		}

		bool hasVisibleRgbData = false;
		const int pixelCount = width * height;
		for (int pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
		{
			const stbi_uc *pixel = pixels + pixelIndex * 4;
			if (pixel[3] == 0)
				continue;
			if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0)
			{
				hasVisibleRgbData = true;
				break;
			}
		}
		if (!hasVisibleRgbData)
		{
			for (int pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
			{
				stbi_uc *pixel = pixels + pixelIndex * 4;
				if (pixel[3] == 0)
					continue;
				pixel[0] = 255;
				pixel[1] = 255;
				pixel[2] = 255;
			}
		}

		unsigned int textureId = 0;
		glGenTextures(1, &textureId);
		glBindTexture(GL_TEXTURE_2D, textureId);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		glBindTexture(GL_TEXTURE_2D, 0);
		stbi_image_free(pixels);

		icon.rendererId = textureId;
		icon.width = width;
		icon.height = height;
		return &icon;
	}

	void RendererLayer::releaseToolbarIcons()
	{
		for (auto &entry : m_ToolbarIcons)
		{
			RendererToolbarIconTexture &icon = entry.second;
			if (icon.rendererId != 0)
			{
				unsigned int textureId = icon.rendererId;
				glDeleteTextures(1, &textureId);
				icon.rendererId = 0;
			}
		}
		m_ToolbarIcons.clear();
	}

	Path RendererLayer::resolveShaderDirectory() const
	{
		if (!m_StartupConfig.assetsDirectory.Empty())
		{
			// Najpierw szukaj shaderów obok binarki (deploy path)
			const Path deployShaders = Path::FromResolved(
				m_StartupConfig.assetsDirectory.Native().parent_path() / "shaders");
			if (FileSystem::Exists(deployShaders.Native()))
				return deployShaders;

			// Fallback: ścieżka deweloperska w repozytorium
			// TODO: usunąć gdy pipeline budowania zawsze kopiuje shadery do deploy dir
		}

		if (!m_StartupConfig.shaderDirectory.Empty())
		{
			const Path resolvedExplicit = Path::FromResolved(m_StartupConfig.shaderDirectory.Native());
			if (FileSystem::Exists(resolvedExplicit.Native()))
				return resolvedExplicit;
		}

		const std::array<Path, 2> candidates = {
			BuildShaderDirectoryFromCurrentPath(),
			BuildShaderDirectoryFromAssetsRoot(m_StartupConfig.assetsDirectory)};

		for (const Path &candidate : candidates)
		{
			if (candidate.Empty())
				continue;
			if (FileSystem::Exists(candidate.Native()))
				return candidate;
		}

		if (!m_StartupConfig.shaderDirectory.Empty())
			return Path::FromResolved(m_StartupConfig.shaderDirectory.Native());
		return candidates[0];
	}
} // namespace DefectStudio
