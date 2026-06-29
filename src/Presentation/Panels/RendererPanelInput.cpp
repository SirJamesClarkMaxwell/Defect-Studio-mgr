#include "Core/dspch.hpp"
#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Events/RendererEvents.hpp"
#include "Presentation/ImGuiInputTranslator.hpp"

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
	}

	void RendererPanel::applyViewportKeyboardNavigation(RendererWindowState &windowState)
	{
		if (windowState.camera == nullptr)
			return;

		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			return;

		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		if (eventBus == nullptr)
			return;

		ImGuiIO &io = ImGui::GetIO();
		if (io.WantTextInput || ImGui::IsAnyItemActive())
			return;

		if (!m_KeyInputProcessor.has_value())
		{
			auto keymapResolver = m_KeymapResolver.lock();
			auto contextManager = m_ContextManager.lock();
			if (keymapResolver != nullptr && contextManager != nullptr)
				m_KeyInputProcessor.emplace(*keymapResolver, *contextManager);
		}
		if (!m_KeyInputProcessor.has_value())
			return;

		std::optional<KeyChord> chord = ImGuiInputTranslator::PollPressedChord();
		if (!chord)
			return;

		auto inputResult = m_KeyInputProcessor->HandleKeyPressed(*chord);
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

	void RendererPanel::onViewportFocusChanged(const std::string &windowId, bool focused)
	{
		(void)windowId;
		auto contextManager = m_ContextManager.lock();
		if (contextManager == nullptr)
			return;

		contextManager->SetActive(kRendererViewportFocusedContext, focused);
	}
} // namespace DefectStudio
