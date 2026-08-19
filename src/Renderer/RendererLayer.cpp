#include "Core/dspch.hpp"

#include "Renderer/RendererLayer.hpp"

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Assert.hpp"

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

#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Time.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
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
	constexpr float kOrbitMouseScale = 0.0065f;
	constexpr float kQuarterTurnRadians = 1.57079632679f;

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

	[[nodiscard]] float EaseOutCubic(float t)
	{
		const float clamped = std::clamp(t, 0.0f, 1.0f);
		const float inv = 1.0f - clamped;
		return 1.0f - inv * inv * inv;
	}

	[[nodiscard]] float RadiansToDegrees(float angleRadians)
	{
		return angleRadians * 57.295779513f;
	}

	[[nodiscard]] RendererViewSnapshot CaptureViewSnapshotFromCamera(const RendererViewCamera &camera)
	{
		RendererViewSnapshot snapshot;
		snapshot.target = camera.Target();
		snapshot.distance = camera.Distance();
		snapshot.yaw = camera.Yaw();
		snapshot.pitch = camera.Pitch();
		snapshot.roll = camera.Roll();
		snapshot.projection = camera.Projection();
		return snapshot;
	}


	RendererLayer::RendererLayer(RendererStartupConfig startupConfig)
		: Layer("RendererLayer"), m_StartupConfig(std::move(startupConfig))
	{
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

	void RendererLayer::AddWindow(RendererWindowState windowState)
	{
		m_Windows.push_back(std::move(windowState));
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

	float RendererLayer::GetLastDeltaTime() const noexcept
	{
		return m_LastDeltaTime;
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

	RenderExportDialogState &RendererLayer::GetExportDialogState()
	{
		return m_ExportDialog;
	}

	bool RendererLayer::CaptureWindowToPng(
		const std::string &windowKey,
		const Path &outputPath,
		std::string &error,
		float cropLeft,
		float cropRight,
		float cropTop,
		float cropBottom) const
	{
		if (m_RendererBackend == nullptr)
		{
			error = "Renderer backend unavailable";
			return false;
		}
		return m_RendererBackend->CaptureWindowToPng(windowKey, outputPath, error, cropLeft, cropRight, cropTop, cropBottom);
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

	void RendererLayer::StartCameraTransition(
		const std::string &windowId,
		const glm::vec3 &target,
		float distance,
		float yaw,
		float pitch,
		float roll,
		const char *sourceAction)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		const char *resolvedSourceAction =
			(sourceAction != nullptr && sourceAction[0] != '\0')
				? sourceAction
				: "unspecified";
		const float targetDistance = std::max(distance, 0.1f);
		if (windowState->transitionActive)
		{
			const float previousDuration = std::max(0.01f, windowState->transitionDuration);
			const float previousProgress =
				std::clamp(windowState->transitionElapsed / previousDuration, 0.0f, 1.0f);
			DS_LOG_DEBUG(
				"Renderer transition interrupted prev_source={} progress={:.3f} new_source={}",
				windowState->transitionSourceAction.empty() ? "unspecified" : windowState->transitionSourceAction.c_str(),
				previousProgress,
				resolvedSourceAction);
		}

		const float startYaw = windowState->camera->Yaw();
		const float startPitch = windowState->camera->Pitch();
		const float startRoll = windowState->camera->Roll();
		const glm::quat startOrientation = RendererViewCamera::CameraOrientationQuatFromEuler(startYaw, startPitch, startRoll);
		glm::quat endOrientation = RendererViewCamera::CameraOrientationQuatFromEuler(yaw, pitch, roll);
		if (glm::dot(startOrientation, endOrientation) < 0.0f)
			endOrientation = -endOrientation;

		const float deltaYaw = RendererViewCamera::NormalizeAngleRadians(yaw - startYaw);
		const float deltaPitch = RendererViewCamera::NormalizeAngleRadians(pitch - startPitch);
		const float deltaRoll = RendererViewCamera::NormalizeAngleRadians(roll - startRoll);
		const float angularDeltaDegrees = RadiansToDegrees(
			2.0f * std::acos(glm::clamp(std::abs(glm::dot(startOrientation, endOrientation)), 0.0f, 1.0f)));

		windowState->transitionActive = true;
		windowState->transitionElapsed = 0.0f;
		windowState->transitionDuration = RendererViewCamera::ComputeTransitionDurationSeconds(
			m_GlobalRenderSettings.rotationSpeed);
		windowState->transitionStartTarget = windowState->camera->Target();
		windowState->transitionEndTarget = target;
		windowState->transitionStartDistance = windowState->camera->Distance();
		windowState->transitionEndDistance = targetDistance;
		windowState->transitionStartYaw = startYaw;
		windowState->transitionEndYaw = yaw;
		windowState->transitionStartPitch = startPitch;
		windowState->transitionEndPitch = pitch;
		windowState->transitionStartRoll = startRoll;
		windowState->transitionEndRoll = roll;
		windowState->transitionStartOrientation = startOrientation;
		windowState->transitionEndOrientation = endOrientation;
		windowState->transitionSourceAction = resolvedSourceAction;

		DS_LOG_DEBUG(
			"Renderer transition start source={} duration={:.3f}s "
			"start_ypr_deg=({:.2f},{:.2f},{:.2f}) end_ypr_deg=({:.2f},{:.2f},{:.2f}) "
			"delta_ypr_deg=({:.2f},{:.2f},{:.2f}) angular_delta_deg={:.2f} distance=({:.3f}->{:.3f})",
			resolvedSourceAction,
			std::max(0.01f, windowState->transitionDuration),
			RadiansToDegrees(startYaw),
			RadiansToDegrees(startPitch),
			RadiansToDegrees(startRoll),
			RadiansToDegrees(yaw),
			RadiansToDegrees(pitch),
			RadiansToDegrees(roll),
			RadiansToDegrees(deltaYaw),
			RadiansToDegrees(deltaPitch),
			RadiansToDegrees(deltaRoll),
			angularDeltaDegrees,
			windowState->camera->Distance(),
			targetDistance);
	}

	void RendererLayer::UpdateCameraTransitions(float deltaTime)
	{
		for (RendererWindowState &windowState : m_Windows)
		{
			if (!windowState.transitionActive || windowState.camera == nullptr)
				continue;

			windowState.transitionElapsed += std::max(0.0f, deltaTime);
			const float duration = std::max(0.01f, windowState.transitionDuration);
			const float alpha = std::clamp(windowState.transitionElapsed / duration, 0.0f, 1.0f);
			const float t = EaseOutCubic(alpha);

			const glm::vec3 target = glm::mix(windowState.transitionStartTarget, windowState.transitionEndTarget, t);
			const float distance = glm::mix(windowState.transitionStartDistance, windowState.transitionEndDistance, t);
			const glm::quat orientation = glm::normalize(
				glm::slerp(windowState.transitionStartOrientation, windowState.transitionEndOrientation, t));

			float yaw = 0.0f;
			float pitch = 0.0f;
			float roll = 0.0f;
			RendererViewCamera::CameraEulerFromOrientationQuat(orientation, yaw, pitch, roll);

			windowState.camera->SetOrbitState(target, distance, yaw, pitch);
			windowState.camera->SetRoll(roll);

			if (alpha >= 1.0f)
			{
				DS_LOG_DEBUG(
					"Renderer transition complete source={} final_ypr_deg=({:.2f},{:.2f},{:.2f})",
					windowState.transitionSourceAction.empty() ? "unspecified" : windowState.transitionSourceAction.c_str(),
					RadiansToDegrees(yaw),
					RadiansToDegrees(pitch),
					RadiansToDegrees(roll));

				windowState.transitionActive = false;
				CommitViewInteraction(windowState.windowId);
			}
		}
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

	void RendererLayer::SetViewportSize(const std::string &windowId, glm::vec2 size)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr)
			return;

		windowState->viewportSize = size;
		if (windowState->camera != nullptr)
			windowState->camera->SetViewport(size.x, size.y);
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

		m_PeriodicTableSymbols = m_StartupConfig.periodicTableSymbols;
		m_LanthanideSymbols = m_StartupConfig.lanthanideSymbols;
		m_ActinideSymbols = m_StartupConfig.actinideSymbols;

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
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::AlignToAxisRequested>(
				std::bind_front(&RendererLayer::onAlignToAxisRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::OrbitDirectionRequested>(
				std::bind_front(&RendererLayer::onOrbitDirectionRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::OrbitQuarterTurnRequested>(
				std::bind_front(&RendererLayer::onOrbitQuarterTurnRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::RollDirectionRequested>(
				std::bind_front(&RendererLayer::onRollDirectionRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::ZoomDirectionRequested>(
				std::bind_front(&RendererLayer::onZoomDirectionRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::OrbitStepRequested>(
				std::bind_front(&RendererLayer::onOrbitStepRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::RollStepRequested>(
				std::bind_front(&RendererLayer::onRollStepRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::ZoomStepRequested>(
				std::bind_front(&RendererLayer::onZoomStepRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::FocusSelectedAtomRequested>(
				std::bind_front(&RendererLayer::onFocusSelectedAtomRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::UndoViewRequested>(
				std::bind_front(&RendererLayer::onUndoViewRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::RedoViewRequested>(
				std::bind_front(&RendererLayer::onRedoViewRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::SaveCurrentViewRequested>(
				std::bind_front(&RendererLayer::onSaveCurrentViewRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::CycleSavedViewRequested>(
				std::bind_front(&RendererLayer::onCycleSavedViewRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::ExportImageRequested>(
				std::bind_front(&RendererLayer::onExportImageRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::ViewTransitionRequested>(
				std::bind_front(&RendererLayer::onViewTransitionRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::ProjectionToggleRequested>(
				std::bind_front(&RendererLayer::onProjectionToggleRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::AtomSelectionRequested>(
				std::bind_front(&RendererLayer::onAtomSelectionRequested, this)));
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
		UpdateCameraTransitions(deltaTime);
	}

	void RendererLayer::OnImGuiRender()
	{
	}

	void RendererLayer::ApplyConfig(const RendererConfig &config)
	{
		const RendererGridSettings previousGridSettings = m_GlobalRenderSettings.grid;

		m_GlobalRenderSettings.backgroundColor = glm::vec4(
			config.backgroundColor[0],
			config.backgroundColor[1],
			config.backgroundColor[2],
			config.backgroundColor[3]);
		m_GlobalRenderSettings.orbitSensitivity = config.orbitSensitivity;
		m_GlobalRenderSettings.panSensitivity = config.panSensitivity;
		m_GlobalRenderSettings.zoomSensitivity = config.zoomSensitivity;
		m_GlobalRenderSettings.rotationSpeed = config.rotationSpeed;
		m_GlobalRenderSettings.focusSelectedAtomDistance = config.focusSelectedAtomDistance;
		m_GlobalRenderSettings.focusSelectedAtomTransitionSeconds = config.focusSelectedAtomTransitionSeconds;
		m_GlobalRenderSettings.focusSelectedAtomRespectAtomRadius = config.focusSelectedAtomRespectAtomRadius;
		m_GlobalRenderSettings.focusSelectedAtomRadiusMultiplier = config.focusSelectedAtomRadiusMultiplier;
		m_GlobalRenderSettings.invertZoom = config.invertZoom;
		m_GlobalRenderSettings.touchpadNavigation = config.touchpadNavigation;
		m_GlobalRenderSettings.defaultCameraProjection = ProjectionFromString(config.defaultProjection);
		m_GlobalRenderSettings.lighting.ambientIntensity = config.lighting.ambientIntensity;
		m_GlobalRenderSettings.lighting.keyIntensity = config.lighting.keyIntensity;
		m_GlobalRenderSettings.lighting.fillIntensity = config.lighting.fillIntensity;
		m_GlobalRenderSettings.lighting.backIntensity = config.lighting.backIntensity;
		m_GlobalRenderSettings.lighting.twoSided = config.lighting.twoSided;
		m_GlobalRenderSettings.lighting.keyDirection = glm::vec3(
			config.lighting.keyDirection[0],
			config.lighting.keyDirection[1],
			config.lighting.keyDirection[2]);
		m_GlobalRenderSettings.lighting.fillDirection = glm::vec3(
			config.lighting.fillDirection[0],
			config.lighting.fillDirection[1],
			config.lighting.fillDirection[2]);
		m_GlobalRenderSettings.lighting.backDirection = glm::vec3(
			config.lighting.backDirection[0],
			config.lighting.backDirection[1],
			config.lighting.backDirection[2]);
		m_GlobalRenderSettings.viewport.axisButtonSize = std::clamp(
			config.viewport.axisButtonSize,
			10.0f,
			48.0f);
		m_GlobalRenderSettings.viewport.iconButtonSize = std::clamp(
			config.viewport.iconButtonSize,
			10.0f,
			48.0f);
		m_GlobalRenderSettings.toolbarWheel.rotationStepDelta = config.toolbarWheel.rotationStepDelta;
		m_GlobalRenderSettings.toolbarWheel.zoomStepDelta = config.toolbarWheel.zoomStepDelta;
		m_GlobalRenderSettings.toolbarWheel.ctrlPresetValues = config.toolbarWheel.ctrlPresetValues;
		m_GlobalRenderSettings.grid.autoFitToStructureBounds = config.grid.autoFitToStructureBounds;
		m_GlobalRenderSettings.grid.paddingPercent = config.grid.paddingPercent;
		m_GlobalRenderSettings.grid.spacing = config.grid.spacing;
		m_GlobalRenderSettings.grid.planeZ = config.grid.planeZ;

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
		DS_LOG_INFO("Renderer startup default scene uses prepared renderer structure data");
		m_Windows = std::move(m_StartupConfig.startupWindows);
		if (m_RendererBackend != nullptr)
			m_RendererBackend->MarkGridDirty();
	}


	void RendererLayer::bindConfigEvents()
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(m_EventBus->Subscribe<RendererEvents::Config::Applied>(
			[this](const RendererEvents::Config::Applied &event) { onConfigApplied(event); }));
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

	RendererWindowState *RendererLayer::findViewportCommandWindow(const std::string &windowId)
	{
		if (!windowId.empty())
			return findWindowById(windowId);
		if (!m_FocusedViewportWindowId.empty())
			return findWindowById(m_FocusedViewportWindowId);
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
		windowState.camera->SetProjection(snapshot.projection);

		const char *resolvedSourceAction =
			(sourceAction != nullptr && sourceAction[0] != '\0')
				? sourceAction
				: "view.restore";
		StartCameraTransition(
			windowState.windowId,
			snapshot.target,
			snapshot.distance,
			snapshot.yaw,
			snapshot.pitch,
			snapshot.roll,
			resolvedSourceAction);
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
		if (event.focused)
			m_FocusedViewportWindowId = event.windowId;
		else if (m_FocusedViewportWindowId == event.windowId)
			m_FocusedViewportWindowId.clear();
		DS_LOG_TRACE("Renderer viewport '{}' focus: {}", event.windowId, event.focused);
	}

	void RendererLayer::onAlignToAxisRequested(const RendererEvents::Viewport::AlignToAxisRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr || event.axis < 0 || event.axis > 2)
			return;

		const glm::vec3 axis = windowState->structure.lattice[static_cast<std::size_t>(event.axis)];
		if (glm::dot(axis, axis) <= 1e-8f)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		RendererViewCamera targetCamera = *windowState->camera;
		targetCamera.SetAlignToAxis(glm::normalize(axis), glm::vec3(0.0f, 0.0f, 1.0f));
		const RendererViewSnapshot after = CaptureViewSnapshotFromCamera(targetCamera);
		pushViewChange(*windowState, before, after, "keyboard.align_axis");
		restoreViewSnapshot(*windowState, after, "keyboard.align_axis");
	}

	void RendererLayer::onOrbitDirectionRequested(const RendererEvents::Viewport::OrbitDirectionRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		const float rotationStepRadians = std::clamp(windowState->rotationStepDeg, 0.0f, 180.0f) * 3.1415926535f / 180.0f;
		const float orbitInputDelta = rotationStepRadians / kOrbitMouseScale;
		RendererEvents::Viewport::OrbitStepRequested stepEvent;
		stepEvent.windowId = windowState->windowId;
		switch (event.direction)
		{
			case RendererEvents::Viewport::OrbitDirection::Left:
				stepEvent.dx = +orbitInputDelta;
				break;
			case RendererEvents::Viewport::OrbitDirection::Right:
				stepEvent.dx = -orbitInputDelta;
				break;
			case RendererEvents::Viewport::OrbitDirection::Up:
				stepEvent.dy = +orbitInputDelta;
				break;
			case RendererEvents::Viewport::OrbitDirection::Down:
				stepEvent.dy = -orbitInputDelta;
				break;
		}
		onOrbitStepRequested(stepEvent);
	}

	void RendererLayer::onOrbitQuarterTurnRequested(const RendererEvents::Viewport::OrbitQuarterTurnRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		const float orbitInputDelta = kQuarterTurnRadians / kOrbitMouseScale;
		RendererEvents::Viewport::OrbitStepRequested stepEvent;
		stepEvent.windowId = windowState->windowId;
		switch (event.direction)
		{
			case RendererEvents::Viewport::OrbitDirection::Left:
				stepEvent.dx = +orbitInputDelta;
				break;
			case RendererEvents::Viewport::OrbitDirection::Right:
				stepEvent.dx = -orbitInputDelta;
				break;
			case RendererEvents::Viewport::OrbitDirection::Up:
				stepEvent.dy = +orbitInputDelta;
				break;
			case RendererEvents::Viewport::OrbitDirection::Down:
				stepEvent.dy = -orbitInputDelta;
				break;
		}
		onOrbitStepRequested(stepEvent);
	}

	void RendererLayer::onRollDirectionRequested(const RendererEvents::Viewport::RollDirectionRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		const float rotationStepRadians = std::clamp(windowState->rotationStepDeg, 0.0f, 180.0f) * 3.1415926535f / 180.0f;
		RendererEvents::Viewport::RollStepRequested stepEvent;
		stepEvent.windowId = windowState->windowId;
		stepEvent.delta = event.direction == RendererEvents::Viewport::RollDirection::Left
			? +rotationStepRadians
			: -rotationStepRadians;
		onRollStepRequested(stepEvent);
	}

	void RendererLayer::onZoomDirectionRequested(const RendererEvents::Viewport::ZoomDirectionRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		const float zoomAmount = std::max(0.5f, windowState->percentStep * 0.1f);
		RendererEvents::Viewport::ZoomStepRequested stepEvent;
		stepEvent.windowId = windowState->windowId;
		stepEvent.amount = event.direction == RendererEvents::Viewport::ZoomDirection::In
			? +zoomAmount
			: -zoomAmount;
		onZoomStepRequested(stepEvent);
	}

	void RendererLayer::onOrbitStepRequested(const RendererEvents::Viewport::OrbitStepRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		RendererViewCamera targetCamera = *windowState->camera;
		targetCamera.Orbit(event.dx, event.dy);
		const RendererViewSnapshot after = CaptureViewSnapshotFromCamera(targetCamera);
		pushViewChange(*windowState, before, after, "keyboard.orbit_step");
		restoreViewSnapshot(*windowState, after, "keyboard.orbit_step");
	}

	void RendererLayer::onRollStepRequested(const RendererEvents::Viewport::RollStepRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		RendererViewCamera targetCamera = *windowState->camera;
		targetCamera.Roll(event.delta);
		const RendererViewSnapshot after = CaptureViewSnapshotFromCamera(targetCamera);
		pushViewChange(*windowState, before, after, "keyboard.roll_step");
		restoreViewSnapshot(*windowState, after, "keyboard.roll_step");
	}

	void RendererLayer::onZoomStepRequested(const RendererEvents::Viewport::ZoomStepRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		windowState->transitionActive = false;
		windowState->camera->Zoom(event.amount);
		const RendererViewSnapshot after = captureViewSnapshot(*windowState);
		pushViewChange(*windowState, before, after, "keyboard.zoom_step");
	}

	void RendererLayer::onFocusSelectedAtomRequested(const RendererEvents::Viewport::FocusSelectedAtomRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr || windowState->selectedAtomIndices.empty())
			return;

		const std::size_t selectedIndex = windowState->selectedAtomIndices.back();
		if (selectedIndex >= windowState->structure.atoms.size())
			return;

		const RendererAtomData &atom = windowState->structure.atoms[selectedIndex];
		float desiredDistance = m_GlobalRenderSettings.focusSelectedAtomDistance;
		if (m_GlobalRenderSettings.focusSelectedAtomRespectAtomRadius)
		{
			const float radiusDistance = atom.radius * m_GlobalRenderSettings.focusSelectedAtomRadiusMultiplier;
			desiredDistance = std::max(desiredDistance, radiusDistance);
		}

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		RendererViewSnapshot after = before;
		after.target = atom.cartesianPosition;
		after.distance = desiredDistance;
		pushViewChange(*windowState, before, after, "keyboard.focus_selected_atom");
		restoreViewSnapshot(*windowState, after, "keyboard.focus_selected_atom");
		windowState->transitionDuration = std::max(
			kMinFocusTransitionSeconds,
			m_GlobalRenderSettings.focusSelectedAtomTransitionSeconds);
	}

	void RendererLayer::onUndoViewRequested(const RendererEvents::Viewport::UndoViewRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState != nullptr)
			UndoViewChange(windowState->windowId);
	}

	void RendererLayer::onRedoViewRequested(const RendererEvents::Viewport::RedoViewRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState != nullptr)
			RedoViewChange(windowState->windowId);
	}

	void RendererLayer::onSaveCurrentViewRequested(const RendererEvents::Viewport::SaveCurrentViewRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		windowState->savedViews.push_back(captureViewSnapshot(*windowState));
		windowState->activeSavedViewIndex = windowState->savedViews.size() - 1u;
	}

	void RendererLayer::onExportImageRequested(const RendererEvents::Viewport::ExportImageRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		// Same setup as the toolbar's "Export PNG..." button - keyboard (F12) and toolbar feed the
		// same dialog rather than each having their own export path. NOTE: does NOT call
		// ImGui::OpenPopup here - this handler runs during input event dispatch, outside any
		// ImGui window's ID stack (ImGui::OpenPopup needs a valid current window / crashes in
		// ImGuiWindow::GetID otherwise). drawExportDialog() opens the popup once it sees `open`.
		m_ExportDialog.open = true;
		m_ExportDialog.targetWindowId = windowState->windowId;
		m_ExportDialog.previewState.camera = CreateUnique<RendererViewCamera>(*windowState->camera);
		m_ExportDialog.previewState.showAtoms = windowState->showAtoms;
		m_ExportDialog.previewState.showBonds = windowState->showBonds;
		m_ExportDialog.previewState.showCellBox = windowState->showCellBox;
		m_ExportDialog.previewState.showGrid = windowState->showGrid;
		m_ExportDialog.previewState.selectedAtomIndices = windowState->selectedAtomIndices;

		try
		{
			const std::string stem = windowState->structure.sourcePath.Native().stem().string();
			m_ExportDialog.filename = (stem.empty() ? "structure" : stem) + "_export";
		}
		catch (const std::exception &exception)
		{
			DS_LOG_ERROR("Export dialog filename derivation failed: {}", exception.what());
			m_ExportDialog.filename = "structure_export";
		}
	}

	void RendererLayer::onCycleSavedViewRequested(const RendererEvents::Viewport::CycleSavedViewRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr || windowState->savedViews.empty())
			return;

		if (windowState->activeSavedViewIndex >= windowState->savedViews.size())
			windowState->activeSavedViewIndex = 0u;
		if (event.direction >= 0)
			windowState->activeSavedViewIndex = (windowState->activeSavedViewIndex + 1u) % windowState->savedViews.size();
		else
			windowState->activeSavedViewIndex =
				(windowState->activeSavedViewIndex + windowState->savedViews.size() - 1u) % windowState->savedViews.size();

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		const RendererViewSnapshot &after = windowState->savedViews[windowState->activeSavedViewIndex];
		pushViewChange(*windowState, before, after, "keyboard.saved_view");
		restoreViewSnapshot(*windowState, after, "keyboard.saved_view");
	}

	void RendererLayer::onViewTransitionRequested(const RendererEvents::Viewport::ViewTransitionRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		windowState->camera->SetProjection(event.targetView.projection);
		BeginViewInteraction(windowState->windowId, event.sourceAction);
		StartCameraTransition(
			windowState->windowId,
			event.targetView.target,
			event.targetView.distance,
			event.targetView.yaw,
			event.targetView.pitch,
			event.targetView.roll,
			event.sourceAction.c_str());
	}

	void RendererLayer::onProjectionToggleRequested(const RendererEvents::Viewport::ProjectionToggleRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		windowState->transitionActive = false;
		windowState->camera->ToggleProjection();
		const RendererViewSnapshot after = captureViewSnapshot(*windowState);
		pushViewChange(*windowState, before, after, "toolbar.toggle_projection");
	}

	void RendererLayer::onAtomSelectionRequested(const RendererEvents::Viewport::AtomSelectionRequested &event)
	{
		RendererWindowState *windowState = findWindowById(event.windowId);
		if (windowState == nullptr)
			return;

		SceneRegistry &scene = windowState->sceneRegistry;

		if (!event.atomIndex.has_value())
		{
			if (!event.additive)
			{
				for (const entt::entity entity : scene.Registry().view<SelectionComponent>())
					scene.Registry().get<SelectionComponent>(entity).selected = false;
				SceneSystem::PushSelectionAndVisibilityToWindowState(scene, *windowState);
			}
			return;
		}

		const std::size_t atomIndex = *event.atomIndex;
		if (atomIndex >= windowState->structure.atoms.size())
			return;

		Entity atomEntity = scene.AtomEntityAt(atomIndex);
		if (!atomEntity)
			return;

		if (!event.additive)
		{
			for (const entt::entity entity : scene.Registry().view<SelectionComponent>())
				scene.Registry().get<SelectionComponent>(entity).selected = false;
			atomEntity.GetComponent<SelectionComponent>().selected = true;
			SceneSystem::PushSelectionAndVisibilityToWindowState(scene, *windowState);
			return;
		}

		SelectionComponent &selection = atomEntity.GetComponent<SelectionComponent>();
		selection.selected = !selection.selected;
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, *windowState);
	}

	void RendererLayer::onConfigApplied(const RendererEvents::Config::Applied &event)
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
