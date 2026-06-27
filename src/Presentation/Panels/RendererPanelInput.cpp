#include "Core/dspch.hpp"
#include "Presentation/Panels/RendererPanel.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/RendererViewCamera.hpp"
namespace DefectStudio
{
	namespace
	{
		constexpr float kOrbitMouseScale = 0.0065f;
		[[nodiscard]] ImGuiKey ToImGuiKey(KeyCode key)
		{
			if (key >= KeyCode::A && key <= KeyCode::Z)
				return static_cast<ImGuiKey>(ImGuiKey_A + (static_cast<int>(key) - static_cast<int>(KeyCode::A)));
			if (key >= KeyCode::D0 && key <= KeyCode::D9)
				return static_cast<ImGuiKey>(ImGuiKey_0 + (static_cast<int>(key) - static_cast<int>(KeyCode::D0)));

			switch (key)
			{
			case KeyCode::Space:
				return ImGuiKey_Space;
			case KeyCode::Comma:
				return ImGuiKey_Comma;
			case KeyCode::Minus:
				return ImGuiKey_Minus;
			case KeyCode::Equal:
				return ImGuiKey_Equal;
			case KeyCode::Period:
				return ImGuiKey_Period;
			case KeyCode::Left:
				return ImGuiKey_LeftArrow;
			case KeyCode::Right:
				return ImGuiKey_RightArrow;
			case KeyCode::Up:
				return ImGuiKey_UpArrow;
			case KeyCode::Down:
				return ImGuiKey_DownArrow;
			default:
				return ImGuiKey_None;
			}
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
		void LogRotationAction(
			const char *actionName,
			float stepDegrees,
			float rotationSpeed,
			float deltaRadians,
			float orbitInputDelta)
		{
			DS_LOG_DEBUG(
				"Renderer rotation action={} step_deg={:.3f} speed={:.3f} delta_rad={:.6f} orbit_input_delta={:.3f}",
				actionName,
				stepDegrees,
				rotationSpeed,
				deltaRadians,
				orbitInputDelta);
		}
		float ComputeCameraTransitionDurationSeconds(float rotationSpeed)
		{
			const float safeSpeed = std::max(0.1f, rotationSpeed);
			return std::clamp(0.14f / safeSpeed, 0.02f, 0.50f);
		}
	}
	void RendererPanel::applyViewportKeyboardNavigation(RendererWindowState &windowState)
	{
		if (windowState.camera == nullptr)
			return;
		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			return;
		ImGuiIO &io = ImGui::GetIO();
		if (io.WantTextInput || ImGui::IsAnyItemActive())
			return;
		const auto queueTransition = [&](const RendererViewCamera &cameraState, const char *sourceAction)
		{
			m_Layer.BeginViewInteraction(windowState.windowId, sourceAction);
			windowState.transitionDuration = ComputeCameraTransitionDurationSeconds(
				m_Layer.GetGlobalSettings().rotationSpeed);
			startCameraTransition(
				windowState,
				cameraState.Target(),
				cameraState.Distance(),
				cameraState.Yaw(),
				cameraState.Pitch(),
				cameraState.Roll(),
				sourceAction);
		};
		const auto alignToAxis = [&](const glm::vec3 &axis)
		{
			if (glm::dot(axis, axis) <= 1e-8f)
				return;
			RendererViewCamera animated = *windowState.camera;
			animated.SetAlignToAxis(glm::normalize(axis), glm::vec3(0.0f, 0.0f, 1.0f));
			queueTransition(animated, "keyboard.align_axis");
		};
		const auto focusSelectedAtom = [&]()
		{
			if (windowState.selectedAtomIndices.empty())
				return;
			const std::size_t selectedIndex = windowState.selectedAtomIndices.back();
			if (selectedIndex >= windowState.structure.atoms.size())
				return;
			const RendererAtomData &atom = windowState.structure.atoms[selectedIndex];
			float desiredDistance = m_Layer.GetGlobalSettings().focusSelectedAtomDistance;
			if (m_Layer.GetGlobalSettings().focusSelectedAtomRespectAtomRadius)
			{
				const float radiusDistance = atom.radius * m_Layer.GetGlobalSettings().focusSelectedAtomRadiusMultiplier;
				desiredDistance = std::max(desiredDistance, radiusDistance);
			}
			windowState.transitionDuration = std::max(
				0.02f,
				m_Layer.GetGlobalSettings().focusSelectedAtomTransitionSeconds);
			m_Layer.BeginViewInteraction(windowState.windowId, "keyboard.focus_selected_atom");
			startCameraTransition(
				windowState,
				atom.cartesianPosition,
				desiredDistance,
				windowState.camera->Yaw(),
				windowState.camera->Pitch(),
				windowState.camera->Roll(),
				"keyboard.focus_selected_atom");
		};
		const float rotationStepRadians = std::clamp(windowState.rotationStepDeg, 0.0f, 180.0f) * 3.1415926535f / 180.0f;
		const float rotationDeltaRadians = rotationStepRadians;
		const float orbitInputDelta = rotationDeltaRadians / kOrbitMouseScale;
		const RendererKeyboardShortcutSettings &shortcuts = m_Layer.GetGlobalSettings().shortcuts;
		const auto shortcutPressed = [](KeyCode key) -> bool
		{
			const ImGuiKey imGuiKey = ToImGuiKey(key);
			return imGuiKey != ImGuiKey_None && ImGui::IsKeyPressed(imGuiKey);
		};
		const auto shortcutDown = [](KeyCode key) -> bool
		{
			const ImGuiKey imGuiKey = ToImGuiKey(key);
			return imGuiKey != ImGuiKey_None && ImGui::IsKeyDown(imGuiKey);
		};
		if (io.KeyCtrl && io.KeyAlt && shortcutPressed(KeyCode::Z))
		{
			if (io.KeyShift)
				m_Layer.RedoViewChange(windowState.windowId);
			else
				m_Layer.UndoViewChange(windowState.windowId);
			return;
		}
		if (io.KeyCtrl && io.KeyAlt && shortcutPressed(KeyCode::Y))
		{
			m_Layer.RedoViewChange(windowState.windowId);
			return;
		}
		const glm::mat3 &lattice = windowState.structure.lattice;
		if (shortcutPressed(shortcuts.alignAxisA))
			alignToAxis(lattice[0]);
		if (shortcutPressed(shortcuts.alignAxisB))
			alignToAxis(lattice[1]);
		if (shortcutPressed(shortcuts.alignAxisC))
			alignToAxis(lattice[2]);
		if (shortcutPressed(shortcuts.orbitLeft))
		{
			LogRotationAction(
				"keyboard.orbit_left",
				windowState.rotationStepDeg,
				m_Layer.GetGlobalSettings().rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(+orbitInputDelta, 0.0f);
			queueTransition(animated, "keyboard.orbit_left");
		}
		if (shortcutPressed(shortcuts.orbitRight))
		{
			LogRotationAction(
				"keyboard.orbit_right",
				windowState.rotationStepDeg,
				m_Layer.GetGlobalSettings().rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(-orbitInputDelta, 0.0f);
			queueTransition(animated, "keyboard.orbit_right");
		}
		if (shortcutPressed(shortcuts.orbitUp))
		{
			LogRotationAction(
				"keyboard.orbit_up",
				windowState.rotationStepDeg,
				m_Layer.GetGlobalSettings().rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, +orbitInputDelta);
			queueTransition(animated, "keyboard.orbit_up");
		}
		if (shortcutPressed(shortcuts.orbitDown))
		{
			LogRotationAction(
				"keyboard.orbit_down",
				windowState.rotationStepDeg,
				m_Layer.GetGlobalSettings().rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Orbit(0.0f, -orbitInputDelta);
			queueTransition(animated, "keyboard.orbit_down");
		}
		if (shortcutPressed(shortcuts.rollLeft))
		{
			LogRotationAction(
				"keyboard.roll_left",
				windowState.rotationStepDeg,
				m_Layer.GetGlobalSettings().rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(+rotationDeltaRadians);
			queueTransition(animated, "keyboard.roll_left");
		}
		if (shortcutPressed(shortcuts.rollRight))
		{
			LogRotationAction(
				"keyboard.roll_right",
				windowState.rotationStepDeg,
				m_Layer.GetGlobalSettings().rotationSpeed,
				rotationDeltaRadians,
				orbitInputDelta);
			RendererViewCamera animated = *windowState.camera;
			animated.Roll(-rotationDeltaRadians);
			queueTransition(animated, "keyboard.roll_right");
		}
		if (shortcutPressed(shortcuts.zoomIn))
		{
			m_Layer.BeginViewInteraction(windowState.windowId, "keyboard.zoom_in");
			windowState.transitionActive = false;
			windowState.camera->Zoom(+std::max(0.5f, windowState.percentStep * 0.1f));
		}
		if (shortcutPressed(shortcuts.zoomOut))
		{
			m_Layer.BeginViewInteraction(windowState.windowId, "keyboard.zoom_out");
			windowState.transitionActive = false;
			windowState.camera->Zoom(-std::max(0.5f, windowState.percentStep * 0.1f));
		}
		if (shortcutPressed(shortcuts.focusSelectedAtom))
			focusSelectedAtom();
		const bool continuousShortcutDown =
			shortcutDown(shortcuts.orbitLeft) ||
			shortcutDown(shortcuts.orbitRight) ||
			shortcutDown(shortcuts.orbitUp) ||
			shortcutDown(shortcuts.orbitDown) ||
			shortcutDown(shortcuts.rollLeft) ||
			shortcutDown(shortcuts.rollRight) ||
			shortcutDown(shortcuts.zoomIn) ||
			shortcutDown(shortcuts.zoomOut);
		if (!continuousShortcutDown &&
			windowState.viewInteractionActive &&
			windowState.viewInteractionSource.rfind("keyboard.", 0) == 0 &&
			!windowState.transitionActive)
		{
			m_Layer.CommitViewInteraction(windowState.windowId);
		}
	}
	void RendererPanel::applyViewportInputNavigation(
		RendererWindowState &windowState,
		const ImVec2 &imageOrigin,
		float deltaTime)
	{
		(void)imageOrigin;
		if (windowState.camera == nullptr)
			return;
		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		ImGuiIO &io = ImGui::GetIO();
		const bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
		const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		const bool altPressed = io.KeyAlt;
		const bool shiftPressed = io.KeyShift;
		const bool touchpadOrbit = m_Layer.GetGlobalSettings().touchpadNavigation && altPressed && lmb;
		const bool touchpadPan = m_Layer.GetGlobalSettings().touchpadNavigation && altPressed && shiftPressed && lmb;
		const bool touchpadZoom = m_Layer.GetGlobalSettings().touchpadNavigation && altPressed && rmb;
		const bool dragActiveInput = mmb || touchpadOrbit || touchpadPan || touchpadZoom;
		if (dragActiveInput)
			windowState.transitionActive = false;
		if (!dragActiveInput)
		{
			windowState.dragActive = false;
			m_LastMousePositions[windowState.windowId] = io.MousePos;
			if (windowState.viewInteractionActive &&
				windowState.viewInteractionSource.rfind("mouse.", 0) == 0)
			{
				m_Layer.CommitViewInteraction(windowState.windowId);
			}
		}
		float wheel = io.MouseWheel;
		if (m_Layer.GetGlobalSettings().invertZoom)
			wheel = -wheel;
		if (wheel != 0.0f)
		{
			m_Layer.BeginViewInteraction(windowState.windowId, "mouse.wheel_zoom");
			windowState.transitionActive = false;
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::ZoomDelta zoomEvent;
				zoomEvent.windowId = windowState.windowId;
				zoomEvent.amount = wheel * m_Layer.GetGlobalSettings().zoomSensitivity;
				eventBus->Publish(zoomEvent);
			}
			m_Layer.CommitViewInteraction(windowState.windowId);
		}
		if (!dragActiveInput)
			return;
		if (!windowState.dragActive)
		{
			const char *sourceAction = touchpadZoom
				? "mouse.touchpad_zoom"
				: ((mmb && shiftPressed) || touchpadPan)
					? "mouse.pan"
					: "mouse.orbit";
			m_Layer.BeginViewInteraction(windowState.windowId, sourceAction);
			windowState.dragActive = true;
			m_LastMousePositions[windowState.windowId] = io.MousePos;
			return;
		}
		ImVec2 &lastMousePosition = m_LastMousePositions[windowState.windowId];
		ImVec2 delta(
			io.MousePos.x - lastMousePosition.x,
			io.MousePos.y - lastMousePosition.y);
		lastMousePosition = io.MousePos;
		const float frameScale = std::max(0.0f, deltaTime) * 60.0f;
		delta.x *= frameScale;
		delta.y *= frameScale;
		if (touchpadZoom)
		{
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::ZoomDelta zoomEvent;
				zoomEvent.windowId = windowState.windowId;
				zoomEvent.amount = (-delta.y * 0.020f) * m_Layer.GetGlobalSettings().zoomSensitivity;
				eventBus->Publish(zoomEvent);
			}
			return;
		}
		if ((mmb && shiftPressed) || touchpadPan)
		{
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::PanDelta panEvent;
				panEvent.windowId = windowState.windowId;
				panEvent.dx = delta.x * m_Layer.GetGlobalSettings().panSensitivity;
				panEvent.dy = delta.y * m_Layer.GetGlobalSettings().panSensitivity;
				eventBus->Publish(panEvent);
			}
			return;
		}
		if (eventBus != nullptr)
		{
			RendererEvents::Viewport::OrbitDelta orbitEvent;
			orbitEvent.windowId = windowState.windowId;
			orbitEvent.dx = delta.x * m_Layer.GetGlobalSettings().orbitSensitivity;
			orbitEvent.dy = delta.y * m_Layer.GetGlobalSettings().orbitSensitivity;
			eventBus->Publish(orbitEvent);
		}
	}
	void RendererPanel::startCameraTransition(
		RendererWindowState &windowState,
		const glm::vec3 &target,
		float distance,
		float yaw,
		float pitch,
		float roll,
		const char *sourceAction)
	{
		if (windowState.camera == nullptr)
			return;
		const char *resolvedSourceAction =
			(sourceAction != nullptr && sourceAction[0] != '\0')
				? sourceAction
				: "unspecified";
		const float targetDistance = std::max(distance, 0.1f);
		if (windowState.transitionActive)
		{
			const float previousDuration = std::max(0.01f, windowState.transitionDuration);
			const float previousProgress =
				std::clamp(windowState.transitionElapsed / previousDuration, 0.0f, 1.0f);
			DS_LOG_DEBUG(
				"Renderer transition interrupted prev_source={} progress={:.3f} new_source={}",
				windowState.transitionSourceAction.empty() ? "unspecified" : windowState.transitionSourceAction.c_str(),
				previousProgress,
				resolvedSourceAction);
		}
		const float startYaw = windowState.camera->Yaw();
		const float startPitch = windowState.camera->Pitch();
		const float startRoll = windowState.camera->Roll();
		const glm::quat startOrientation = RendererViewCamera::CameraOrientationQuatFromEuler(startYaw, startPitch, startRoll);
		glm::quat endOrientation = RendererViewCamera::CameraOrientationQuatFromEuler(yaw, pitch, roll);
		if (glm::dot(startOrientation, endOrientation) < 0.0f)
			endOrientation = -endOrientation;
		const float deltaYaw = RendererViewCamera::NormalizeAngleRadians(yaw - startYaw);
		const float deltaPitch = RendererViewCamera::NormalizeAngleRadians(pitch - startPitch);
		const float deltaRoll = RendererViewCamera::NormalizeAngleRadians(roll - startRoll);
		const float angularDeltaDegrees = RadiansToDegrees(
			2.0f * std::acos(glm::clamp(std::abs(glm::dot(startOrientation, endOrientation)), 0.0f, 1.0f)));
		DS_LOG_DEBUG(
			"Renderer transition start source={} duration={:.3f}s "
			"start_ypr_deg=({:.2f},{:.2f},{:.2f}) end_ypr_deg=({:.2f},{:.2f},{:.2f}) "
			"delta_ypr_deg=({:.2f},{:.2f},{:.2f}) angular_delta_deg={:.2f} distance=({:.3f}->{:.3f})",
			resolvedSourceAction,
			std::max(0.01f, windowState.transitionDuration),
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
			windowState.camera->Distance(),
			targetDistance);
		windowState.transitionActive = true;
		windowState.transitionElapsed = 0.0f;
		windowState.transitionStartTarget = windowState.camera->Target();
		windowState.transitionEndTarget = target;
		windowState.transitionStartDistance = windowState.camera->Distance();
		windowState.transitionEndDistance = targetDistance;
		windowState.transitionStartYaw = startYaw;
		windowState.transitionEndYaw = yaw;
		windowState.transitionStartPitch = startPitch;
		windowState.transitionEndPitch = pitch;
		windowState.transitionStartRoll = startRoll;
		windowState.transitionEndRoll = roll;
		windowState.transitionStartOrientation = startOrientation;
		windowState.transitionEndOrientation = endOrientation;
		windowState.transitionSourceAction = resolvedSourceAction;
	}
	void RendererPanel::updateCameraTransition(RendererWindowState &windowState, float deltaTime)
	{
		if (!windowState.transitionActive || windowState.camera == nullptr)
			return;
		windowState.transitionElapsed += std::max(0.0f, deltaTime);
		const float duration = std::max(0.01f, windowState.transitionDuration);
		const float alpha = std::clamp(windowState.transitionElapsed / duration, 0.0f, 1.0f);
		const float t = EaseOutCubic(alpha);
		const glm::vec3 target = glm::mix(windowState.transitionStartTarget, windowState.transitionEndTarget, t);
		const float distance = glm::mix(windowState.transitionStartDistance, windowState.transitionEndDistance, t);
		const glm::quat orientation =
			glm::normalize(glm::slerp(windowState.transitionStartOrientation, windowState.transitionEndOrientation, t));
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
			m_Layer.CommitViewInteraction(windowState.windowId);
		}
	}
} // namespace DefectStudio
