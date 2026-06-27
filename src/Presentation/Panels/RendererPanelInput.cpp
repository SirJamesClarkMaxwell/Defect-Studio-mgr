#include "Core/dspch.hpp"
#include "Presentation/Panels/RendererPanel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeyBinding.hpp"
#include "Core/Input/KeyInputProcessor.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/RendererViewCamera.hpp"
namespace DefectStudio
{
	namespace
	{
		constexpr float kOrbitMouseScale = 0.0065f;
		constexpr const char *kRendererViewportFocusedContext = "renderer.viewport.focused";
		constexpr const char *kCommandAlignAxisA = "renderer.align_axis_a";
		constexpr const char *kCommandAlignAxisB = "renderer.align_axis_b";
		constexpr const char *kCommandAlignAxisC = "renderer.align_axis_c";
		constexpr const char *kCommandOrbitLeft = "renderer.orbit_left";
		constexpr const char *kCommandOrbitRight = "renderer.orbit_right";
		constexpr const char *kCommandOrbitUp = "renderer.orbit_up";
		constexpr const char *kCommandOrbitDown = "renderer.orbit_down";
		constexpr const char *kCommandRollLeft = "renderer.roll_left";
		constexpr const char *kCommandRollRight = "renderer.roll_right";
		constexpr const char *kCommandZoomIn = "renderer.zoom_in";
		constexpr const char *kCommandZoomOut = "renderer.zoom_out";
		constexpr const char *kCommandFocusSelectedAtom = "renderer.focus_selected_atom";
		constexpr const char *kCommandUndoView = "renderer.undo_view";
		constexpr const char *kCommandRedoView = "renderer.redo_view";

		[[nodiscard]] std::optional<KeyCode> ToKeyCode(ImGuiKey key)
		{
			if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
				return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (static_cast<int>(key) - static_cast<int>(ImGuiKey_A)));
			if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
				return static_cast<KeyCode>(static_cast<int>(KeyCode::D0) + (static_cast<int>(key) - static_cast<int>(ImGuiKey_0)));

			switch (key)
			{
			case ImGuiKey_Space:
				return KeyCode::Space;
			case ImGuiKey_Comma:
				return KeyCode::Comma;
			case ImGuiKey_Minus:
				return KeyCode::Minus;
			case ImGuiKey_Equal:
				return KeyCode::Equal;
			case ImGuiKey_Period:
				return KeyCode::Period;
			case ImGuiKey_LeftArrow:
				return KeyCode::Left;
			case ImGuiKey_RightArrow:
				return KeyCode::Right;
			case ImGuiKey_UpArrow:
				return KeyCode::Up;
			case ImGuiKey_DownArrow:
				return KeyCode::Down;
			case ImGuiKey_Escape:
				return KeyCode::Escape;
			case ImGuiKey_Enter:
				return KeyCode::Enter;
			case ImGuiKey_Tab:
				return KeyCode::Tab;
			case ImGuiKey_Backspace:
				return KeyCode::Backspace;
			case ImGuiKey_Delete:
				return KeyCode::Delete;
			case ImGuiKey_Insert:
				return KeyCode::Insert;
			case ImGuiKey_Home:
				return KeyCode::Home;
			case ImGuiKey_End:
				return KeyCode::End;
			case ImGuiKey_PageUp:
				return KeyCode::PageUp;
			case ImGuiKey_PageDown:
				return KeyCode::PageDown;
			default:
				break;
			}

			if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12)
				return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + (static_cast<int>(key) - static_cast<int>(ImGuiKey_F1)));
			return std::nullopt;
		}

		[[nodiscard]] KeyModifiers CurrentImGuiModifiers()
		{
			ImGuiIO &io = ImGui::GetIO();
			KeyModifiers modifiers = KeyModifiers::None;
			if (io.KeyCtrl)
				modifiers = modifiers | KeyModifiers::Ctrl;
			if (io.KeyShift)
				modifiers = modifiers | KeyModifiers::Shift;
			if (io.KeyAlt)
				modifiers = modifiers | KeyModifiers::Alt;
			if (io.KeySuper)
				modifiers = modifiers | KeyModifiers::Super;
			return modifiers;
		}

		[[nodiscard]] std::optional<KeyChord> PollPressedKeyChord()
		{
			constexpr std::array<ImGuiKey, 67> keys = {
				ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F,
				ImGuiKey_G, ImGuiKey_H, ImGuiKey_I, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L,
				ImGuiKey_M, ImGuiKey_N, ImGuiKey_O, ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R,
				ImGuiKey_S, ImGuiKey_T, ImGuiKey_U, ImGuiKey_V, ImGuiKey_W, ImGuiKey_X,
				ImGuiKey_Y, ImGuiKey_Z,
				ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
				ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
				ImGuiKey_Space, ImGuiKey_Comma, ImGuiKey_Minus, ImGuiKey_Equal, ImGuiKey_Period,
				ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow,
				ImGuiKey_Escape, ImGuiKey_Enter, ImGuiKey_Tab, ImGuiKey_Backspace,
				ImGuiKey_Delete, ImGuiKey_Insert, ImGuiKey_Home, ImGuiKey_End,
				ImGuiKey_PageUp, ImGuiKey_PageDown,
				ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5, ImGuiKey_F6,
				ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9, ImGuiKey_F10, ImGuiKey_F11, ImGuiKey_F12};

			for (const ImGuiKey key : keys)
			{
				if (!ImGui::IsKeyPressed(key))
					continue;
				std::optional<KeyCode> keyCode = ToKeyCode(key);
				if (!keyCode)
					continue;
				return KeyChord{*keyCode, CurrentImGuiModifiers()};
			}
			return std::nullopt;
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
	}
	void RendererPanel::applyViewportKeyboardNavigation(RendererWindowState &windowState)
	{
		if (windowState.camera == nullptr)
			return;

		auto contextManager = m_ContextManager.lock();
		auto keymapResolver = m_KeymapResolver.lock();
		Ref<EventBus> eventBus = m_EventBus != nullptr ? m_EventBus : m_Layer.GetEventBus();
		if (contextManager == nullptr || keymapResolver == nullptr || eventBus == nullptr)
			return;

		contextManager->SetActive(kRendererViewportFocusedContext, false);
		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			return;

		ImGuiIO &io = ImGui::GetIO();
		if (io.WantTextInput || ImGui::IsAnyItemActive())
			return;

		contextManager->SetActive(kRendererViewportFocusedContext, true);
		const auto clearViewportContext = [&]() {
			contextManager->SetActive(kRendererViewportFocusedContext, false);
		};

		std::optional<KeyChord> chord = PollPressedKeyChord();
		if (!chord)
		{
			clearViewportContext();
			return;
		}

		KeyInputProcessor inputProcessor(*keymapResolver, *contextManager);
		auto inputResult = inputProcessor.HandleKeyPressed(*chord);
		clearViewportContext();
		if (!inputResult || !inputResult->handled || !inputResult->commandId)
			return;

		const float rotationStepRadians = std::clamp(windowState.rotationStepDeg, 0.0f, 180.0f) * 3.1415926535f / 180.0f;
		const float orbitInputDelta = rotationStepRadians / kOrbitMouseScale;
		const float zoomAmount = std::max(0.5f, windowState.percentStep * 0.1f);
		const std::string &commandId = inputResult->commandId->value;

		if (commandId == kCommandAlignAxisA || commandId == kCommandAlignAxisB || commandId == kCommandAlignAxisC)
		{
			RendererEvents::Viewport::AlignToAxisRequested event;
			event.windowId = windowState.windowId;
			event.axis = commandId == kCommandAlignAxisA ? 0 : (commandId == kCommandAlignAxisB ? 1 : 2);
			eventBus->Publish(event);
			return;
		}

		if (commandId == kCommandOrbitLeft || commandId == kCommandOrbitRight || commandId == kCommandOrbitUp || commandId == kCommandOrbitDown)
		{
			RendererEvents::Viewport::OrbitStepRequested event;
			event.windowId = windowState.windowId;
			if (commandId == kCommandOrbitLeft)
				event.dx = +orbitInputDelta;
			else if (commandId == kCommandOrbitRight)
				event.dx = -orbitInputDelta;
			else if (commandId == kCommandOrbitUp)
				event.dy = +orbitInputDelta;
			else
				event.dy = -orbitInputDelta;
			eventBus->Publish(event);
			return;
		}

		if (commandId == kCommandRollLeft || commandId == kCommandRollRight)
		{
			RendererEvents::Viewport::RollStepRequested event;
			event.windowId = windowState.windowId;
			event.delta = commandId == kCommandRollLeft ? +rotationStepRadians : -rotationStepRadians;
			eventBus->Publish(event);
			return;
		}

		if (commandId == kCommandZoomIn || commandId == kCommandZoomOut)
		{
			RendererEvents::Viewport::ZoomStepRequested event;
			event.windowId = windowState.windowId;
			event.amount = commandId == kCommandZoomIn ? +zoomAmount : -zoomAmount;
			eventBus->Publish(event);
			return;
		}

		if (commandId == kCommandFocusSelectedAtom)
		{
			RendererEvents::Viewport::FocusSelectedAtomRequested event;
			event.windowId = windowState.windowId;
			eventBus->Publish(event);
			return;
		}

		if (commandId == kCommandUndoView)
		{
			RendererEvents::Viewport::UndoViewRequested event;
			event.windowId = windowState.windowId;
			eventBus->Publish(event);
			return;
		}

		if (commandId == kCommandRedoView)
		{
			RendererEvents::Viewport::RedoViewRequested event;
			event.windowId = windowState.windowId;
			eventBus->Publish(event);
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
