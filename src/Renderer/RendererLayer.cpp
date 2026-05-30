#include "Core/dspch.hpp"

#include "Renderer/RendererLayer.hpp"

#include "App/ApplicationState.hpp"
#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string_view>

#include <glad/gl.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Time.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/RendererQuickTestBootstrap.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kMinSensitivity = 0.05f;
		constexpr float kMaxSensitivity = 4.0f;
		constexpr float kMinFocusDistance = 0.25f;
		constexpr float kMaxFocusDistance = 256.0f;
		constexpr float kMinFocusTransitionSeconds = 0.02f;
		constexpr float kMaxFocusTransitionSeconds = 3.0f;
		constexpr float kMinFocusRadiusMultiplier = 0.1f;
		constexpr float kMaxFocusRadiusMultiplier = 32.0f;

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

	}

	RendererLayer::RendererLayer(RendererQuickTestRuntime runtime)
		: Layer("RendererLayer"), m_Runtime(std::move(runtime))
	{
		m_Panel = CreateUnique<RendererPanel>(*this);
	}

	RendererLayer::~RendererLayer() = default;

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

		Result<void> initializeResult = m_RendererBackend->Initialize(shaderDirectory);
		if (!initializeResult.HasValue())
		{
			DS_LOG_ERROR(
				"RendererLayer initialization failed: {}",
				initializeResult.Error().technicalDetails);
			m_RendererBackend.reset();
			return;
		}

		if (m_Runtime.enableQuickTestingStartup)
			loadQuickTestWindows();
		applyDefaultProjectionToWindows();
		if (m_Runtime.eventBus != nullptr)
		{
			m_EventBus = m_Runtime.eventBus;
			bindConfigEvents();
		}
		m_Attached = true;
		DS_LOG_INFO("Renderer shader root: {}", shaderDirectory.String());
		DS_LOG_INFO("RendererLayer attached with {} quick-test windows", m_Windows.size());
	}

	void RendererLayer::OnDetach()
	{
		releaseToolbarIcons();
		ClearSubscriptions();
		m_EventBus.reset();
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
		m_GlobalRenderSettings.backgroundColor = glm::vec4(
			config.renderer.backgroundColor[0],
			config.renderer.backgroundColor[1],
			config.renderer.backgroundColor[2],
			config.renderer.backgroundColor[3]);
		m_GlobalRenderSettings.orbitSensitivity = config.renderer.orbitSensitivity;
		m_GlobalRenderSettings.panSensitivity = config.renderer.panSensitivity;
		m_GlobalRenderSettings.zoomSensitivity = config.renderer.zoomSensitivity;
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
		applyDefaultProjectionToWindows();
	}

	void RendererLayer::loadQuickTestWindows()
	{
		m_Windows = BuildRendererQuickTestWindows(m_Runtime.assetsDirectory);
	}


	void RendererLayer::bindConfigEvents()
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(m_EventBus->Subscribe<AppEvents::Config::Applied>(
			[this](const AppEvents::Config::Applied &event) { onConfigApplied(event); }));
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

	const RendererToolbarIconTexture *RendererLayer::getToolbarIcon(const std::string &iconFileName)
	{
		if (iconFileName.empty())
			return nullptr;

		RendererToolbarIconTexture &icon = m_ToolbarIcons[iconFileName];
		if (icon.loadAttempted)
			return icon.rendererId != 0 ? &icon : nullptr;

		icon.loadAttempted = true;

		const Path iconPath = m_Runtime.assetsDirectory / Path("icons") / Path(iconFileName);
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
		if (!m_Runtime.shaderDirectory.Empty())
		{
			const Path resolvedExplicit = Path::FromResolved(m_Runtime.shaderDirectory.Native());
			if (FileSystem::Exists(resolvedExplicit.Native()))
				return resolvedExplicit;
		}

		const std::array<Path, 2> candidates = {
			BuildShaderDirectoryFromCurrentPath(),
			BuildShaderDirectoryFromAssetsRoot(m_Runtime.assetsDirectory)};

		for (const Path &candidate : candidates)
		{
			if (candidate.Empty())
				continue;
			if (FileSystem::Exists(candidate.Native()))
				return candidate;
		}

		if (!m_Runtime.shaderDirectory.Empty())
			return Path::FromResolved(m_Runtime.shaderDirectory.Native());
		return candidates[0];
	}
} // namespace DefectStudio
