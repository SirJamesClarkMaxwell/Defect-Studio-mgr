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
#include <unordered_set>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glad/gl.h>
#include <stb_image.h>

#include <sstream>

#include "Core/Logging/Logger.hpp"
#include "Core/Platform/PlatformPaths.hpp"
#include "Core/Utils/Time.hpp"
#include "IO/TextFileIO.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/ViewModifier.hpp"
#include "Domain/Electronic/ElectronicStructureModel.hpp"

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

	[[nodiscard]] Path DefaultViewStatePath()
	{
		return Path::FromResolved(
			FileSystem::CurrentPath() / "install" / "users" / "default" / "config" / "renderer_default_view.txt");
	}

	[[nodiscard]] Path SavedViewsStatePath()
	{
		return Path::FromResolved(
			FileSystem::CurrentPath() / "install" / "users" / "default" / "config" / "renderer_saved_views.txt");
	}

	[[nodiscard]] std::string SerializePositionList(const std::vector<glm::vec3> &positions)
	{
		std::ostringstream stream;
		for (std::size_t i = 0; i < positions.size(); ++i)
		{
			if (i != 0)
				stream << ';';
			stream << positions[i].x << ':' << positions[i].y << ':' << positions[i].z;
		}
		return stream.str();
	}

	[[nodiscard]] std::vector<glm::vec3> DeserializePositionList(const std::string &text)
	{
		std::vector<glm::vec3> result;
		std::size_t start = 0;
		while (start < text.size())
		{
			const std::size_t semi = text.find(';', start);
			const std::string token = text.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
			const std::size_t c1 = token.find(':');
			const std::size_t c2 = c1 == std::string::npos ? std::string::npos : token.find(':', c1 + 1);
			if (c1 != std::string::npos && c2 != std::string::npos)
			{
				try
				{
					result.emplace_back(
						std::stof(token.substr(0, c1)),
						std::stof(token.substr(c1 + 1, c2 - c1 - 1)),
						std::stof(token.substr(c2 + 1)));
				}
				catch (const std::exception &)
				{
				}
			}
			if (semi == std::string::npos)
				break;
			start = semi + 1;
		}
		return result;
	}

	// "cam-fields|selected-positions|hidden-positions|name" - selection/hidden are position lists
	// (not raw indices), same convention as restoreViewSnapshot's cross-structure resolve: for a
	// same-file restore this resolves back to the exact original indices at ~zero distance, so one
	// format covers both the shared/cross-window saved views and the per-window project-state
	// restore. Segments 2/3/4 are optional - a bare "|"-free line (this format's first shipped
	// shape) still parses fine as camera-only with empty selection/hidden/name.
	[[nodiscard]] std::string SerializeViewSnapshot(const RendererViewSnapshot &snapshot)
	{
		// '|' is the segment delimiter and this is a one-line-per-entry file, so strip anything
		// that would corrupt either.
		std::string sanitizedName = snapshot.name;
		std::replace_if(
			sanitizedName.begin(), sanitizedName.end(),
			[](char c) { return c == '|' || c == '\n' || c == '\r'; },
			' ');

		std::ostringstream stream;
		stream << snapshot.target.x << ',' << snapshot.target.y << ',' << snapshot.target.z << ','
			   << snapshot.distance << ',' << snapshot.yaw << ',' << snapshot.pitch << ',' << snapshot.roll << ','
			   << static_cast<int>(snapshot.projection) << '|' << SerializePositionList(snapshot.selectedAtomPositions)
			   << '|' << SerializePositionList(snapshot.hiddenAtomPositions) << '|' << sanitizedName;
		return stream.str();
	}

	[[nodiscard]] std::optional<RendererViewSnapshot> DeserializeViewSnapshot(const std::string &line)
	{
		std::vector<std::string> segments;
		std::size_t segStart = 0;
		while (true)
		{
			const std::size_t bar = line.find('|', segStart);
			segments.push_back(line.substr(segStart, bar == std::string::npos ? std::string::npos : bar - segStart));
			if (bar == std::string::npos)
				break;
			segStart = bar + 1;
		}

		std::vector<std::string> fields;
		std::size_t start = 0;
		while (true)
		{
			const std::size_t comma = segments[0].find(',', start);
			fields.push_back(
				segments[0].substr(start, comma == std::string::npos ? std::string::npos : comma - start));
			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
		if (fields.size() != 8)
			return std::nullopt;

		try
		{
			RendererViewSnapshot snapshot;
			snapshot.target = glm::vec3(std::stof(fields[0]), std::stof(fields[1]), std::stof(fields[2]));
			snapshot.distance = std::stof(fields[3]);
			snapshot.yaw = std::stof(fields[4]);
			snapshot.pitch = std::stof(fields[5]);
			snapshot.roll = std::stof(fields[6]);
			snapshot.projection =
				std::stoi(fields[7]) == 1 ? CameraProjection::Orthographic : CameraProjection::Perspective;
			if (segments.size() > 1)
				snapshot.selectedAtomPositions = DeserializePositionList(segments[1]);
			if (segments.size() > 2)
				snapshot.hiddenAtomPositions = DeserializePositionList(segments[2]);
			if (segments.size() > 3)
				snapshot.name = segments[3];
			return snapshot;
		}
		catch (const std::exception &)
		{
			return std::nullopt;
		}
	}

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

	// Camera-only capture - the three keyboard step handlers below build `after` from a scratch
	// RendererViewCamera copy, not from the real windowState, so selection/visibility can't be
	// read off it directly. `selectionSource` (always the handler's own `before` snapshot, since
	// none of these operations touch selection/visibility) carries those fields through instead
	// of leaving them default-empty, which previously made restoreViewSnapshot wipe selection and
	// reveal every hidden atom on every align-axis/orbit-step/roll-step keypress.
	[[nodiscard]] RendererViewSnapshot CaptureViewSnapshotFromCamera(
		const RendererViewCamera &camera, const RendererViewSnapshot &selectionSource)
	{
		RendererViewSnapshot snapshot;
		snapshot.target = camera.Target();
		snapshot.distance = camera.Distance();
		snapshot.yaw = camera.Yaw();
		snapshot.pitch = camera.Pitch();
		snapshot.roll = camera.Roll();
		snapshot.projection = camera.Projection();
		snapshot.selectedAtomIndices = selectionSource.selectedAtomIndices;
		snapshot.hiddenAtomIndices = selectionSource.hiddenAtomIndices;
		snapshot.selectedAtomPositions = selectionSource.selectedAtomPositions;
		snapshot.hiddenAtomPositions = selectionSource.hiddenAtomPositions;
		return snapshot;
	}


	RendererLayer::RendererLayer(RendererStartupConfig startupConfig)
		: Layer("RendererLayer"), m_StartupConfig(std::move(startupConfig))
	{
		loadPersistedViews();
	}

	void RendererLayer::loadPersistedViews()
	{
		std::string text;
		std::string error;
		if (TextFileIO::Load(DefaultViewStatePath(), text, error) && !text.empty())
		{
			std::istringstream stream(text);
			std::string line;
			if (std::getline(stream, line))
			{
				if (std::optional<RendererViewSnapshot> snapshot = DeserializeViewSnapshot(line))
					m_SessionDefaultView = *snapshot;
			}
		}

		if (TextFileIO::Load(SavedViewsStatePath(), text, error))
		{
			std::istringstream stream(text);
			std::string line;
			while (std::getline(stream, line))
			{
				if (std::optional<RendererViewSnapshot> snapshot = DeserializeViewSnapshot(line))
					m_SharedSavedViews.push_back(*snapshot);
			}
		}
	}

	void RendererLayer::savePersistedDefaultView()
	{
		if (!m_SessionDefaultView.has_value())
			return;
		std::string error;
		if (!TextFileIO::Save(DefaultViewStatePath(), SerializeViewSnapshot(*m_SessionDefaultView), error))
			DS_LOG_WARN("RendererLayer: failed to persist default view: {}", error);
	}

	void RendererLayer::savePersistedSharedViews()
	{
		std::ostringstream stream;
		for (const RendererViewSnapshot &snapshot : m_SharedSavedViews)
			stream << SerializeViewSnapshot(snapshot) << '\n';
		std::string error;
		if (!TextFileIO::Save(SavedViewsStatePath(), stream.str(), error))
			DS_LOG_WARN("RendererLayer: failed to persist saved views: {}", error);
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
		// windowId is now deterministic (hashed from the source path, see
		// RendererStartupBootstrap::GenerateRendererWindowId) so persisted per-window state and
		// imgui.ini docking layout survive a restart - but that means opening the exact same file
		// twice in one session would otherwise collide two live windows onto one ImGui window
		// identity. This is the one place that can see what's already open, so it's the one place
		// that can catch and resolve that collision.
		if (findWindowById(windowState.windowId) != nullptr)
		{
			const std::string baseId = windowState.windowId;
			int suffix = 2;
			do
			{
				windowState.windowId = baseId + "-" + std::to_string(suffix);
				++suffix;
			} while (findWindowById(windowState.windowId) != nullptr);
		}

		m_Windows.push_back(std::move(windowState));
		if (m_GlobalRenderSettings.autoApplyDefaultViewOnOpen && m_SessionDefaultView.has_value())
		{
			RendererWindowState &newWindow = m_Windows.back();
			if (newWindow.camera != nullptr)
				restoreViewSnapshot(newWindow, *m_SessionDefaultView, "startup.apply_default_view");
		}
	}

	void RendererLayer::RemoveWindow(const std::string &windowId)
	{
		m_Windows.erase(
			std::remove_if(
				m_Windows.begin(), m_Windows.end(),
				[&windowId](const RendererWindowState &candidate) { return candidate.windowId == windowId; }),
			m_Windows.end());
		if (m_FocusedViewportWindowId == windowId)
			m_FocusedViewportWindowId.clear();
		if (m_LastFocusedViewportWindowId == windowId)
			m_LastFocusedViewportWindowId.clear();
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

	const std::string &RendererLayer::GetFocusedViewportWindowId() const noexcept
	{
		return m_FocusedViewportWindowId;
	}

	const std::string &RendererLayer::GetLastFocusedViewportWindowId() const noexcept
	{
		return m_LastFocusedViewportWindowId;
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
			windowState.showLabels,
			windowState.pinnedMeasurements,
			windowState.selectedPinnedMeasurement,
			windowState.selectedAtomIndices,
			windowState.selectedBondIndices,
			nullptr,
			&windowState.orbitalChannelUp,
			&windowState.orbitalChannelDown);
	}

	int RendererLayer::RegenerateOrbitalIsosurface(
		const std::string &windowId, const OrbitalGridData &grid, float isoValue, int slot)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr || m_RendererBackend == nullptr)
			return 0;

		RendererWindowState::OrbitalOverlayChannel &channel =
			slot == 0 ? windowState->orbitalChannelUp : windowState->orbitalChannelDown;
		channel.vertexCount = m_RendererBackend->RegenerateIsosurfaceGpu(windowId, grid, isoValue, slot);
		return channel.vertexCount;
	}

	int RendererLayer::RegenerateOrbitalIsosurfaceForChannel(
		const std::string &windowKey,
		const OrbitalGridData &grid,
		float isoValue,
		int slot,
		RendererWindowState::OrbitalOverlayChannel &channel)
	{
		if (m_RendererBackend == nullptr)
			return 0;

		channel.vertexCount = m_RendererBackend->RegenerateIsosurfaceGpu(windowKey, grid, isoValue, slot);
		return channel.vertexCount;
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

	bool &RendererLayer::GetPeriodicTableApplyOnConfirm()
	{
		return m_PeriodicTableApplyOnConfirm;
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
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::PanDirectionRequested>(
				std::bind_front(&RendererLayer::onPanDirectionRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::PanStepRequested>(
				std::bind_front(&RendererLayer::onPanStepRequested, this)));
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
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::UndoLabelsRequested>(
				std::bind_front(&RendererLayer::onUndoLabelsRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::RedoLabelsRequested>(
				std::bind_front(&RendererLayer::onRedoLabelsRequested, this)));
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
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::BondSelectionRequested>(
				std::bind_front(&RendererLayer::onBondSelectionRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::SelectionToolToggleRequested>(
				std::bind_front(&RendererLayer::onSelectionToolToggleRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::SelectionModeSetRequested>(
				std::bind_front(&RendererLayer::onSelectionModeSetRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::GizmoOperationRequested>(
				std::bind_front(&RendererLayer::onGizmoOperationRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::AddAtomPopupToggleRequested>(
				std::bind_front(&RendererLayer::onAddAtomPopupToggleRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsToggleRequested>(
				std::bind_front(&RendererLayer::onLabelsToggleRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsToggleSelectedBondRequested>(
				std::bind_front(&RendererLayer::onLabelsToggleSelectedBondRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsToggleSelectedAngleRequested>(
				std::bind_front(&RendererLayer::onLabelsToggleSelectedAngleRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsRemoveSelectedBondRequested>(
				std::bind_front(&RendererLayer::onLabelsRemoveSelectedBondRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsShowAllBondRequested>(
				std::bind_front(&RendererLayer::onLabelsShowAllBondRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsRemoveAllBondRequested>(
				std::bind_front(&RendererLayer::onLabelsRemoveAllBondRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsRemoveSelectedAngleRequested>(
				std::bind_front(&RendererLayer::onLabelsRemoveSelectedAngleRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsShowAllAngleRequested>(
				std::bind_front(&RendererLayer::onLabelsShowAllAngleRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsRemoveAllAngleRequested>(
				std::bind_front(&RendererLayer::onLabelsRemoveAllAngleRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::LabelsToggleBondAlignmentRequested>(
				std::bind_front(&RendererLayer::onLabelsToggleBondAlignmentRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::RegionSelectionRequested>(
				std::bind_front(&RendererLayer::onRegionSelectionRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::HideSelectionRequested>(
				std::bind_front(&RendererLayer::onHideSelectionRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::ShowAllRequested>(
				std::bind_front(&RendererLayer::onShowAllRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::SelectionInvertRequested>(
				std::bind_front(&RendererLayer::onSelectionInvertRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::SelectAllRequested>(
				std::bind_front(&RendererLayer::onSelectAllRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::Cursor3DSetPositionRequested>(
				std::bind_front(&RendererLayer::onCursor3DSetPositionRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::SetAsDefaultViewRequested>(
				std::bind_front(&RendererLayer::onSetAsDefaultViewRequested, this)));
			AddSubscription(m_EventBus->Subscribe<RendererEvents::Viewport::ApplyDefaultViewRequested>(
				std::bind_front(&RendererLayer::onApplyDefaultViewRequested, this)));
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
		m_GlobalRenderSettings.autoApplyDefaultViewOnOpen = config.autoApplyDefaultViewOnOpen;
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

		snapshot.selectedAtomIndices = windowState.selectedAtomIndices;
		for (const std::size_t index : snapshot.selectedAtomIndices)
			if (index < windowState.structure.atoms.size())
				snapshot.selectedAtomPositions.push_back(windowState.structure.atoms[index].cartesianPosition);
		for (std::size_t index = 0; index < windowState.structure.atoms.size(); ++index)
		{
			if (!windowState.structure.atoms[index].visible)
			{
				snapshot.hiddenAtomIndices.push_back(index);
				snapshot.hiddenAtomPositions.push_back(windowState.structure.atoms[index].cartesianPosition);
			}
		}
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

		// Resolve by position, not by reusing snapshot indices directly: restoring commonly
		// crosses structures (session default view copy/pasted to a different window, or
		// auto-applied to a newly-opened one), where atom N in one structure isn't atom N in
		// another. For same-structure restores (undo/redo, align-axis, cycle-saved-view, ...) this
		// resolves back to the exact same indices at ~zero distance, so one code path covers both.
		const std::vector<std::size_t> resolvedSelected = SceneSystem::ResolveAtomIndicesByPosition(
			windowState.structure, snapshot.selectedAtomPositions);
		const std::vector<std::size_t> resolvedHidden = SceneSystem::ResolveAtomIndicesByPosition(
			windowState.structure, snapshot.hiddenAtomPositions);
		SceneSystem::ApplySelectionAndVisibilityToScene(windowState.sceneRegistry, resolvedSelected, resolvedHidden);
		SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry, windowState);

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
		const bool sameSelection = before.selectedAtomIndices == after.selectedAtomIndices;
		const bool sameVisibility = before.hiddenAtomIndices == after.hiddenAtomIndices;
		if (sameTarget && sameScalars && sameSelection && sameVisibility)
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
		{
			m_FocusedViewportWindowId = event.windowId;
			m_LastFocusedViewportWindowId = event.windowId;
		}
		else if (m_FocusedViewportWindowId == event.windowId)
			m_FocusedViewportWindowId.clear();
		DS_LOG_TRACE("Renderer viewport '{}' focus: {}", event.windowId, event.focused);
	}

	void RendererLayer::onAlignToAxisRequested(const RendererEvents::Viewport::AlignToAxisRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr || event.axis < 0 || event.axis > 5)
			return;

		// axis 0-2 = a/b/c (real lattice), 3-5 = a*/b*/c* (reciprocal lattice) - mirrors the
		// toolbar axis buttons (RendererPanelToolbar.cpp), which read the same two matrices.
		const bool isReciprocal = event.axis > 2;
		const glm::mat3 &basis = isReciprocal ? windowState->structure.reciprocalLattice : windowState->structure.lattice;
		const glm::vec3 axis = basis[static_cast<std::size_t>(event.axis - (isReciprocal ? 3 : 0))];
		if (glm::dot(axis, axis) <= 1e-8f)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		RendererViewCamera targetCamera = *windowState->camera;
		targetCamera.SetAlignToAxis(glm::normalize(axis), glm::vec3(0.0f, 0.0f, 1.0f));
		const RendererViewSnapshot after = CaptureViewSnapshotFromCamera(targetCamera, before);
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
		const RendererViewSnapshot after = CaptureViewSnapshotFromCamera(targetCamera, before);
		pushViewChange(*windowState, before, after, "keyboard.orbit_step");
		restoreViewSnapshot(*windowState, after, "keyboard.orbit_step");
	}

	void RendererLayer::onPanDirectionRequested(const RendererEvents::Viewport::PanDirectionRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		// Same up/down sign convention as the toolbar's pan buttons (RendererPanelToolbar.cpp) - up
		// is negative Y in RendererViewCamera::Pan's own screen-pixel space.
		RendererEvents::Viewport::PanStepRequested stepEvent;
		stepEvent.windowId = windowState->windowId;
		switch (event.direction)
		{
			case RendererEvents::Viewport::OrbitDirection::Left: stepEvent.dx = -windowState->pixelStepPx; break;
			case RendererEvents::Viewport::OrbitDirection::Right: stepEvent.dx = +windowState->pixelStepPx; break;
			case RendererEvents::Viewport::OrbitDirection::Up: stepEvent.dy = -windowState->pixelStepPx; break;
			case RendererEvents::Viewport::OrbitDirection::Down: stepEvent.dy = +windowState->pixelStepPx; break;
		}
		onPanStepRequested(stepEvent);
	}

	void RendererLayer::onPanStepRequested(const RendererEvents::Viewport::PanStepRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		RendererViewCamera targetCamera = *windowState->camera;
		targetCamera.Pan(event.dx, event.dy);
		const RendererViewSnapshot after = CaptureViewSnapshotFromCamera(targetCamera, before);
		pushViewChange(*windowState, before, after, "keyboard.pan_step");
		restoreViewSnapshot(*windowState, after, "keyboard.pan_step");
	}

	void RendererLayer::onRollStepRequested(const RendererEvents::Viewport::RollStepRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		RendererViewCamera targetCamera = *windowState->camera;
		targetCamera.Roll(event.delta);
		const RendererViewSnapshot after = CaptureViewSnapshotFromCamera(targetCamera, before);
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

	void RendererLayer::onUndoLabelsRequested(const RendererEvents::Viewport::UndoLabelsRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState != nullptr)
			UndoLabelsChange(windowState->windowId);
	}

	void RendererLayer::onRedoLabelsRequested(const RendererEvents::Viewport::RedoLabelsRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState != nullptr)
			RedoLabelsChange(windowState->windowId);
	}

	namespace
	{
		constexpr std::size_t kMaxPinnedMeasurementHistoryEntries = 64;
	} // namespace

	void PushPinnedMeasurementUndoSnapshot(RendererWindowState &windowState)
	{
		windowState.pinnedMeasurementUndoHistory.push_back(windowState.pinnedMeasurements);
		if (windowState.pinnedMeasurementUndoHistory.size() > kMaxPinnedMeasurementHistoryEntries)
			windowState.pinnedMeasurementUndoHistory.erase(windowState.pinnedMeasurementUndoHistory.begin());
		windowState.pinnedMeasurementRedoHistory.clear();
	}

	void RendererLayer::UndoLabelsChange(const std::string &windowId)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr || windowState->pinnedMeasurementUndoHistory.empty())
			return;

		windowState->pinnedMeasurementRedoHistory.push_back(windowState->pinnedMeasurements);
		windowState->pinnedMeasurements = std::move(windowState->pinnedMeasurementUndoHistory.back());
		windowState->pinnedMeasurementUndoHistory.pop_back();
		// Selection index isn't meaningfully preserved across an undo (the restored vector may have a
		// different size/order than what was selected a moment ago) - same simple reset RemovePinsWithinSet
		// already does when the selected pin itself is the one that disappears.
		windowState->selectedPinnedMeasurement = -1;
		windowState->labelGizmoDragging = false;
		windowState->labelGizmoAxis = -1;
		windowState->pinnedMeasurementDragging = false;
		SceneSystem::SyncLabelEntities(windowState->sceneRegistry, *windowState);
	}

	void RendererLayer::RedoLabelsChange(const std::string &windowId)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState == nullptr || windowState->pinnedMeasurementRedoHistory.empty())
			return;

		windowState->pinnedMeasurementUndoHistory.push_back(windowState->pinnedMeasurements);
		windowState->pinnedMeasurements = std::move(windowState->pinnedMeasurementRedoHistory.back());
		windowState->pinnedMeasurementRedoHistory.pop_back();
		windowState->selectedPinnedMeasurement = -1;
		windowState->labelGizmoDragging = false;
		windowState->labelGizmoAxis = -1;
		windowState->pinnedMeasurementDragging = false;
		SceneSystem::SyncLabelEntities(windowState->sceneRegistry, *windowState);
	}

	void RendererLayer::onSaveCurrentViewRequested(const RendererEvents::Viewport::SaveCurrentViewRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		RendererViewSnapshot snapshot = captureViewSnapshot(*windowState);
		snapshot.name = "View " + std::to_string(m_SharedSavedViews.size() + 1u);
		m_SharedSavedViews.push_back(std::move(snapshot));
		m_ActiveSharedSavedViewIndex = m_SharedSavedViews.size() - 1u;
		savePersistedSharedViews();
	}

	const std::vector<RendererViewSnapshot> &RendererLayer::GetSharedSavedViews() const
	{
		return m_SharedSavedViews;
	}

	void RendererLayer::RenameSharedSavedView(std::size_t index, std::string name)
	{
		if (index >= m_SharedSavedViews.size())
			return;
		m_SharedSavedViews[index].name = std::move(name);
		savePersistedSharedViews();
	}

	void RendererLayer::DeleteSharedSavedView(std::size_t index)
	{
		if (index >= m_SharedSavedViews.size())
			return;
		m_SharedSavedViews.erase(m_SharedSavedViews.begin() + static_cast<std::ptrdiff_t>(index));
		if (!m_SharedSavedViews.empty() && m_ActiveSharedSavedViewIndex >= m_SharedSavedViews.size())
			m_ActiveSharedSavedViewIndex = m_SharedSavedViews.size() - 1u;
		savePersistedSharedViews();
	}

	void RendererLayer::MoveSharedSavedView(std::size_t index, int direction)
	{
		if (index >= m_SharedSavedViews.size())
			return;

		if (direction < 0)
		{
			if (index == 0)
				return;
			std::swap(m_SharedSavedViews[index], m_SharedSavedViews[index - 1u]);
		}
		else if (direction > 0)
		{
			if (index + 1u >= m_SharedSavedViews.size())
				return;
			std::swap(m_SharedSavedViews[index], m_SharedSavedViews[index + 1u]);
		}
		else
		{
			return;
		}
		savePersistedSharedViews();
	}

	void RendererLayer::ApplySharedSavedView(std::size_t index, const std::string &windowId)
	{
		if (index >= m_SharedSavedViews.size())
			return;

		RendererWindowState *windowState =
			findWindowById(windowId.empty() ? m_LastFocusedViewportWindowId : windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		m_ActiveSharedSavedViewIndex = index;
		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		const RendererViewSnapshot &after = m_SharedSavedViews[index];
		pushViewChange(*windowState, before, after, "panel.saved_view");
		restoreViewSnapshot(*windowState, after, "panel.saved_view");
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
		// Pinned bond/angle labels were rendered live but silently dropped from every export until
		// now - previewState is a fresh RendererWindowState, not a copy of the real window, so its
		// pinnedMeasurements stayed empty and renderLabels() had nothing to draw regardless of the
		// "Labels" checkbox. No selection highlight in the exported image (selectedPinnedMeasurement
		// left at its default -1) since a click-selection is interaction state, not part of the scene.
		m_ExportDialog.previewState.pinnedMeasurements = windowState->pinnedMeasurements;
		m_ExportDialog.previewState.bondLabelsAlignToDirection = windowState->bondLabelsAlignToDirection;

		m_ExportDialog.filename = windowState->title.empty() ? "structure" : windowState->title;
	}

	void RendererLayer::onCycleSavedViewRequested(const RendererEvents::Viewport::CycleSavedViewRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr || m_SharedSavedViews.empty())
			return;

		if (m_ActiveSharedSavedViewIndex >= m_SharedSavedViews.size())
			m_ActiveSharedSavedViewIndex = 0u;
		if (event.direction >= 0)
			m_ActiveSharedSavedViewIndex = (m_ActiveSharedSavedViewIndex + 1u) % m_SharedSavedViews.size();
		else
			m_ActiveSharedSavedViewIndex =
				(m_ActiveSharedSavedViewIndex + m_SharedSavedViews.size() - 1u) % m_SharedSavedViews.size();

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		const RendererViewSnapshot &after = m_SharedSavedViews[m_ActiveSharedSavedViewIndex];
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

	void RendererLayer::onBondSelectionRequested(const RendererEvents::Viewport::BondSelectionRequested &event)
	{
		RendererWindowState *windowState = findWindowById(event.windowId);
		if (windowState == nullptr)
			return;

		SceneRegistry &scene = windowState->sceneRegistry;

		if (!event.bondIndex.has_value())
		{
			if (!event.additive)
			{
				for (const entt::entity entity : scene.Registry().view<SelectionComponent>())
					scene.Registry().get<SelectionComponent>(entity).selected = false;
				SceneSystem::PushSelectionAndVisibilityToWindowState(scene, *windowState);
			}
			return;
		}

		const std::size_t bondIndex = *event.bondIndex;
		if (bondIndex >= windowState->structure.bonds.size())
			return;

		Entity bondEntity = scene.BondEntityAt(bondIndex);
		if (!bondEntity)
			return;

		if (!event.additive)
		{
			for (const entt::entity entity : scene.Registry().view<SelectionComponent>())
				scene.Registry().get<SelectionComponent>(entity).selected = false;
			bondEntity.GetComponent<SelectionComponent>().selected = true;
			SceneSystem::PushSelectionAndVisibilityToWindowState(scene, *windowState);
			return;
		}

		SelectionComponent &selection = bondEntity.GetComponent<SelectionComponent>();
		selection.selected = !selection.selected;
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, *windowState);
	}

	void RendererLayer::onSelectionToolToggleRequested(const RendererEvents::Viewport::SelectionToolToggleRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		const SelectionToolMode newTool =
			(windowState->activeSelectionTool == event.tool) ? SelectionToolMode::None : event.tool;
		// Entering a measure tool starts from a clean pick - leftover selection from whatever tool
		// was active before would otherwise silently join the first click as part of the pair/triple.
		if (newTool == SelectionToolMode::MeasureBond || newTool == SelectionToolMode::MeasureAngle)
		{
			for (const entt::entity entity : windowState->sceneRegistry.Registry().view<SelectionComponent>())
				windowState->sceneRegistry.Registry().get<SelectionComponent>(entity).selected = false;
			SceneSystem::PushSelectionAndVisibilityToWindowState(windowState->sceneRegistry, *windowState);
		}
		windowState->activeSelectionTool = newTool;
		windowState->selectionDragActive = false;
	}

	void RendererLayer::onSelectionModeSetRequested(const RendererEvents::Viewport::SelectionModeSetRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		windowState->pickAtoms = event.pickAtoms;
		windowState->pickBonds = event.pickBonds;
		windowState->pickLabels = event.pickLabels;
	}

	void RendererLayer::onGizmoOperationRequested(const RendererEvents::Viewport::GizmoOperationRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		windowState->gizmoOperation = event.operation;
	}

	void RendererLayer::onAddAtomPopupToggleRequested(const RendererEvents::Viewport::AddAtomPopupToggleRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		windowState->addAtomPopupRequested = true;
	}

	void RendererLayer::onLabelsToggleRequested(const RendererEvents::Viewport::LabelsToggleRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		windowState->showLabels = !windowState->showLabels;
	}

	namespace
	{
		// A pin's identity is its atom SET, not the order the caller happened to list them in -
		// callers pass raw bond endpoints or raw selection order, so this sorts before
		// comparing/storing. removeIfPresent=true (the single-pair M/Shift+M press) toggles: pressing
		// the same set again removes it. removeIfPresent=false (bulk M/Shift+M over a multi-atom
		// selection) is add-only: re-selecting a pair/triple that's already pinned leaves it alone
		// instead of unpinning it - a bulk press used to be able to both add AND remove within the
		// same press depending on what was already pinned in the selection, which made "select more
		// atoms, press M again" an unpredictable mix of adding new labels and deleting existing ones.
		// periodicOffset is RendererBondData::secondAtomPeriodicOffset, pointing atomIndices[0] (as
		// passed in, BEFORE the identity sort below) -> atomIndices[1] - only meaningful for a 2-atom
		// bond pin; angle pins (size 3) ignore it. Distinguishes bonds to different periodic images of
		// the same neighbor (see PinnedMeasurement::bondPeriodicOffset) so bulk-pinning a selection
		// doesn't collapse two real bonds onto one toggle identity.
		void ToggleMeasurementPin(
			RendererWindowState &windowState, std::vector<std::size_t> atomIndices, glm::vec3 periodicOffset = glm::vec3(0.0f),
			bool removeIfPresent = true)
		{
			if (atomIndices.size() == 2)
			{
				if (atomIndices[0] > atomIndices[1])
				{
					std::swap(atomIndices[0], atomIndices[1]);
					periodicOffset = -periodicOffset;
				}
			}
			else
			{
				std::sort(atomIndices.begin(), atomIndices.end());
			}

			constexpr float kPeriodicOffsetEpsilon = 1.0e-3f;
			auto existing = std::find_if(
				windowState.pinnedMeasurements.begin(),
				windowState.pinnedMeasurements.end(),
				[&](const RendererWindowState::PinnedMeasurement &pin) {
					if (pin.atomIndices != atomIndices)
						return false;
					return atomIndices.size() != 2 ||
						glm::distance(pin.bondPeriodicOffset, periodicOffset) < kPeriodicOffsetEpsilon;
				});
			if (existing != windowState.pinnedMeasurements.end())
			{
				if (!removeIfPresent)
					return;
				const auto removedIndex = std::distance(windowState.pinnedMeasurements.begin(), existing);
				windowState.pinnedMeasurements.erase(existing);
				if (windowState.selectedPinnedMeasurement == static_cast<int>(removedIndex))
					windowState.selectedPinnedMeasurement = -1;
				else if (windowState.selectedPinnedMeasurement > static_cast<int>(removedIndex))
					--windowState.selectedPinnedMeasurement;
			}
			else
			{
				RendererWindowState::PinnedMeasurement pin;
				pin.atomIndices = std::move(atomIndices);
				pin.alignToBondDirection = pin.atomIndices.size() == 2 && windowState.bondLabelsAlignToDirection;
				pin.bondPeriodicOffset = periodicOffset;
				windowState.pinnedMeasurements.push_back(std::move(pin));
			}
		}

		[[nodiscard]] std::unordered_set<std::size_t> VisibleAtomsIn(
			const RendererWindowState &windowState, const std::vector<std::size_t> &atomIndices)
		{
			std::unordered_set<std::size_t> result;
			for (const std::size_t atomIndex : atomIndices)
			{
				if (atomIndex < windowState.structure.atoms.size() && windowState.structure.atoms[atomIndex].visible)
					result.insert(atomIndex);
			}
			return result;
		}

		[[nodiscard]] std::unordered_set<std::size_t> AllVisibleAtoms(const RendererWindowState &windowState)
		{
			std::unordered_set<std::size_t> result;
			for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
			{
				if (windowState.structure.atoms[i].visible)
					result.insert(i);
			}
			return result;
		}

		// Pins every bonded pair WITHIN atomSet, not just some fixed pair - with 4+ atoms in the set
		// this pins a length for each actual bond among them in one press. Add-only (see
		// ToggleMeasurementPin's removeIfPresent note).
		void AddBondPinsWithinSet(RendererWindowState &windowState, const std::unordered_set<std::size_t> &atomSet)
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			const std::size_t countBefore = windowState.pinnedMeasurements.size();
			bool pinnedAny = false;
			for (const RendererBondData &bond : windowState.structure.bonds)
			{
				if (atomSet.contains(bond.firstAtomIndex) && atomSet.contains(bond.secondAtomIndex))
				{
					ToggleMeasurementPin(
						windowState, {bond.firstAtomIndex, bond.secondAtomIndex}, bond.secondAtomPeriodicOffset,
						/*removeIfPresent=*/false);
					pinnedAny = true;
				}
			}
			// Falls back to a raw 2-point distance pin when the pair has no actual bond in the model -
			// mirrors AddAnglePinsWithinSet's 3-point fallback below. Without this, measuring the
			// distance between two atoms the auto-bond cutoff doesn't connect (e.g. two atoms across a
			// defect) silently did nothing, since the loop above only ever matches real bonds.
			if (!pinnedAny && atomSet.size() == 2)
				ToggleMeasurementPin(
					windowState, std::vector<std::size_t>(atomSet.begin(), atomSet.end()), glm::vec3(0.0f),
					/*removeIfPresent=*/false);
			// Add-only, so a pin count unchanged from countBefore means nothing was actually added
			// (every bonded pair in the selection was already pinned) - drop the snapshot pushed above
			// rather than leave a no-op entry in the undo history (repeatedly pressing M/Ctrl+M over an
			// already-fully-pinned selection is a common way to hit this).
			if (windowState.pinnedMeasurements.size() == countBefore)
				windowState.pinnedMeasurementUndoHistory.pop_back();
			// One resync after the whole batch, not per pin inside the loop above - SyncLabelEntities
			// destroys/recreates every label entity, so doing it per-toggle would be O(pins²) for a
			// bulk press over a large selection.
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		}

		// With more than 3 atoms in atomSet, pins one angle per actual bonded pair-at-a-vertex within
		// it (mirrors AddBondPinsWithinSet's "every bonded pair" generalization) - e.g. a whole ring
		// labels every internal angle in one press. Falls back to the plain 3-point angle (vertex =
		// whichever is bonded to both others, or the middle index if none are bonded at all - see
		// ResolveAngleVertexIndex) only when exactly 3 atoms are in the set and none are bonded to
		// each other, so a free-floating 3-point angle still works. Add-only.
		void AddAnglePinsWithinSet(RendererWindowState &windowState, const std::unordered_set<std::size_t> &atomSet)
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			const std::size_t countBefore = windowState.pinnedMeasurements.size();
			std::unordered_map<std::size_t, std::vector<std::size_t>> neighborsByAtom;
			for (const RendererBondData &bond : windowState.structure.bonds)
			{
				if (!atomSet.contains(bond.firstAtomIndex) || !atomSet.contains(bond.secondAtomIndex))
					continue;
				neighborsByAtom[bond.firstAtomIndex].push_back(bond.secondAtomIndex);
				neighborsByAtom[bond.secondAtomIndex].push_back(bond.firstAtomIndex);
			}

			bool pinnedAny = false;
			for (const auto &[vertex, neighbors] : neighborsByAtom)
			{
				for (std::size_t i = 0; i < neighbors.size(); ++i)
				{
					for (std::size_t j = i + 1; j < neighbors.size(); ++j)
					{
						ToggleMeasurementPin(windowState, {vertex, neighbors[i], neighbors[j]}, glm::vec3(0.0f), /*removeIfPresent=*/false);
						pinnedAny = true;
					}
				}
			}

			if (!pinnedAny && atomSet.size() == 3)
				ToggleMeasurementPin(
					windowState, std::vector<std::size_t>(atomSet.begin(), atomSet.end()), glm::vec3(0.0f),
					/*removeIfPresent=*/false);

			// See AddBondPinsWithinSet's matching comment - drop the no-op snapshot if nothing was
			// actually added.
			if (windowState.pinnedMeasurements.size() == countBefore)
				windowState.pinnedMeasurementUndoHistory.pop_back();
			// One resync after the whole batch - see AddBondPinsWithinSet's matching comment.
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		}

		// Removes every existing pin of the given size (2 = bond, 3 = angle) whose atoms are ALL
		// members of atomSet - filters what's already pinned rather than recomputing bond candidates,
		// so it also cleans up a pin left over from a structure edit that no longer has a matching
		// bond.
		void RemovePinsWithinSet(RendererWindowState &windowState, std::size_t pinSize, const std::unordered_set<std::size_t> &atomSet)
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			const std::size_t countBefore = windowState.pinnedMeasurements.size();
			std::vector<RendererWindowState::PinnedMeasurement> &pins = windowState.pinnedMeasurements;
			for (std::size_t i = 0; i < pins.size();)
			{
				const RendererWindowState::PinnedMeasurement &pin = pins[i];
				const bool matches = pin.atomIndices.size() == pinSize &&
					std::all_of(pin.atomIndices.begin(), pin.atomIndices.end(),
						[&](const std::size_t atomIndex) { return atomSet.contains(atomIndex); });
				if (!matches)
				{
					++i;
					continue;
				}
				pins.erase(pins.begin() + static_cast<std::ptrdiff_t>(i));
				if (windowState.selectedPinnedMeasurement == static_cast<int>(i))
					windowState.selectedPinnedMeasurement = -1;
				else if (windowState.selectedPinnedMeasurement > static_cast<int>(i))
					--windowState.selectedPinnedMeasurement;
			}
			// See AddBondPinsWithinSet's matching comment - drop the no-op snapshot if nothing matched.
			if (windowState.pinnedMeasurements.size() == countBefore)
				windowState.pinnedMeasurementUndoHistory.pop_back();
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		}
	} // namespace

	void RendererLayer::onLabelsToggleSelectedBondRequested(
		const RendererEvents::Viewport::LabelsToggleSelectedBondRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;
		// M with nothing selected activates the Measure Bond tool (click 2 atoms in the viewport)
		// instead of silently no-op'ing - matches clicking the toolbar button, see
		// onSelectionToolToggleRequested for the actual toggle/clear-selection logic.
		if (windowState->selectedAtomIndices.empty())
		{
			RendererEvents::Viewport::SelectionToolToggleRequested toggleEvent;
			toggleEvent.windowId = windowState->windowId;
			toggleEvent.tool = SelectionToolMode::MeasureBond;
			onSelectionToolToggleRequested(toggleEvent);
			return;
		}
		if (windowState->selectedAtomIndices.size() < 2)
			return;
		AddBondPinsWithinSet(*windowState, VisibleAtomsIn(*windowState, windowState->selectedAtomIndices));
	}

	void RendererLayer::onLabelsRemoveSelectedBondRequested(
		const RendererEvents::Viewport::LabelsRemoveSelectedBondRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;
		RemovePinsWithinSet(*windowState, 2, VisibleAtomsIn(*windowState, windowState->selectedAtomIndices));
	}

	void RendererLayer::onLabelsShowAllBondRequested(const RendererEvents::Viewport::LabelsShowAllBondRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;
		AddBondPinsWithinSet(*windowState, AllVisibleAtoms(*windowState));
	}

	void RendererLayer::onLabelsRemoveAllBondRequested(const RendererEvents::Viewport::LabelsRemoveAllBondRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;
		RemovePinsWithinSet(*windowState, 2, AllVisibleAtoms(*windowState));
	}

	void RendererLayer::onLabelsToggleSelectedAngleRequested(
		const RendererEvents::Viewport::LabelsToggleSelectedAngleRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;
		// Shift+M with nothing selected activates the Measure Angle tool - see the matching comment
		// in onLabelsToggleSelectedBondRequested.
		if (windowState->selectedAtomIndices.empty())
		{
			RendererEvents::Viewport::SelectionToolToggleRequested toggleEvent;
			toggleEvent.windowId = windowState->windowId;
			toggleEvent.tool = SelectionToolMode::MeasureAngle;
			onSelectionToolToggleRequested(toggleEvent);
			return;
		}
		if (windowState->selectedAtomIndices.size() < 3)
			return;
		AddAnglePinsWithinSet(*windowState, VisibleAtomsIn(*windowState, windowState->selectedAtomIndices));
	}

	void RendererLayer::onLabelsRemoveSelectedAngleRequested(
		const RendererEvents::Viewport::LabelsRemoveSelectedAngleRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;
		RemovePinsWithinSet(*windowState, 3, VisibleAtomsIn(*windowState, windowState->selectedAtomIndices));
	}

	void RendererLayer::onLabelsShowAllAngleRequested(const RendererEvents::Viewport::LabelsShowAllAngleRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;
		AddAnglePinsWithinSet(*windowState, AllVisibleAtoms(*windowState));
	}

	void RendererLayer::onLabelsRemoveAllAngleRequested(const RendererEvents::Viewport::LabelsRemoveAllAngleRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;
		RemovePinsWithinSet(*windowState, 3, AllVisibleAtoms(*windowState));
	}

	void RendererLayer::onLabelsToggleBondAlignmentRequested(
		const RendererEvents::Viewport::LabelsToggleBondAlignmentRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;
		windowState->bondLabelsAlignToDirection = !windowState->bondLabelsAlignToDirection;
		for (RendererWindowState::PinnedMeasurement &pin : windowState->pinnedMeasurements)
		{
			if (pin.atomIndices.size() == 2)
				pin.alignToBondDirection = windowState->bondLabelsAlignToDirection;
		}
	}

	void RendererLayer::onRegionSelectionRequested(const RendererEvents::Viewport::RegionSelectionRequested &event)
	{
		RendererWindowState *windowState = findWindowById(event.windowId);
		if (windowState == nullptr)
			return;

		SceneRegistry &scene = windowState->sceneRegistry;
		entt::registry &registry = scene.Registry();

		if (event.mode == RendererEvents::Viewport::RegionSelectMode::Replace)
			for (const entt::entity entity : registry.view<SelectionComponent>())
				registry.get<SelectionComponent>(entity).selected = false;

		const bool selectedValue = event.mode != RendererEvents::Viewport::RegionSelectMode::Subtract;
		for (const std::size_t atomIndex : event.atomIndices)
		{
			Entity atomEntity = scene.AtomEntityAt(atomIndex);
			if (atomEntity)
				atomEntity.GetComponent<SelectionComponent>().selected = selectedValue;
		}
		for (const std::size_t bondIndex : event.bondIndices)
		{
			Entity bondEntity = scene.BondEntityAt(bondIndex);
			if (bondEntity)
				bondEntity.GetComponent<SelectionComponent>().selected = selectedValue;
		}
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, *windowState);
	}

	void RendererLayer::onHideSelectionRequested(const RendererEvents::Viewport::HideSelectionRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		HideSelectionModifier{}.Apply(windowState->sceneRegistry, *windowState);
		const RendererViewSnapshot after = captureViewSnapshot(*windowState);
		pushViewChange(*windowState, before, after, "keyboard.hide_selection");
	}

	void RendererLayer::onShowAllRequested(const RendererEvents::Viewport::ShowAllRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		ShowAllModifier{}.Apply(windowState->sceneRegistry, *windowState);
		const RendererViewSnapshot after = captureViewSnapshot(*windowState);
		pushViewChange(*windowState, before, after, "keyboard.show_all");
	}

	void RendererLayer::onSelectionInvertRequested(const RendererEvents::Viewport::SelectionInvertRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		InvertSelectionModifier{}.Apply(windowState->sceneRegistry, *windowState);
		const RendererViewSnapshot after = captureViewSnapshot(*windowState);
		pushViewChange(*windowState, before, after, "keyboard.invert_selection");
	}

	void RendererLayer::onSelectAllRequested(const RendererEvents::Viewport::SelectAllRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		// Only visible atoms - selecting hidden ones too would let a subsequent M/gizmo/delete act on
		// atoms the user can't see or intended to exclude via H (matches InvertSelectionModifier's
		// same visible-only rule below).
		SceneRegistry &scene = windowState->sceneRegistry;
		entt::registry &registry = scene.Registry();
		for (const entt::entity entity : registry.view<SelectionComponent, const VisibilityComponent>())
			registry.get<SelectionComponent>(entity).selected = registry.get<const VisibilityComponent>(entity).visible;
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, *windowState);
	}

	void RendererLayer::onCursor3DSetPositionRequested(const RendererEvents::Viewport::Cursor3DSetPositionRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr)
			return;

		windowState->cursor3DPosition = event.position;
		windowState->cursor3DPlaced = true;
	}

	std::optional<RendererViewSnapshot> RendererLayer::CaptureWindowViewSnapshot(const std::string &windowId) const
	{
		for (const RendererWindowState &candidate : m_Windows)
		{
			if (candidate.windowId == windowId && candidate.camera != nullptr)
				return captureViewSnapshot(candidate);
		}
		return std::nullopt;
	}

	void RendererLayer::ApplyWindowViewSnapshot(const std::string &windowId, const RendererViewSnapshot &snapshot)
	{
		RendererWindowState *windowState = findWindowById(windowId);
		if (windowState != nullptr)
			restoreViewSnapshot(*windowState, snapshot, "project_state.restore");
	}

	void RendererLayer::onSetAsDefaultViewRequested(const RendererEvents::Viewport::SetAsDefaultViewRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr)
			return;

		m_SessionDefaultView = captureViewSnapshot(*windowState);
		savePersistedDefaultView();
	}

	void RendererLayer::onApplyDefaultViewRequested(const RendererEvents::Viewport::ApplyDefaultViewRequested &event)
	{
		RendererWindowState *windowState = findViewportCommandWindow(event.windowId);
		if (windowState == nullptr || windowState->camera == nullptr || !m_SessionDefaultView.has_value())
			return;

		const RendererViewSnapshot before = captureViewSnapshot(*windowState);
		const RendererViewSnapshot &after = *m_SessionDefaultView;
		pushViewChange(*windowState, before, after, "keyboard.apply_default_view");
		restoreViewSnapshot(*windowState, after, "keyboard.apply_default_view");
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
		// Najpierw szukaj shaderów obok binarki (deploy path) — premake kopiuje je do
		// %{cfg.targetdir}/shaders, czyli obok samego .exe, nie obok install/app/assets.
		const Path executableDirectory = Platform::GetExecutableDirectory();
		if (!executableDirectory.Empty())
		{
			const Path deployShaders = Path::FromResolved(executableDirectory.Native() / "shaders");
			if (FileSystem::Exists(deployShaders.Native()))
				return deployShaders;
		}

		// Fallback: ścieżka deweloperska w repozytorium (CWD=repo root, np. odpalone z VS)
		// TODO: usunąć gdy pipeline budowania zawsze kopiuje shadery do deploy dir

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
