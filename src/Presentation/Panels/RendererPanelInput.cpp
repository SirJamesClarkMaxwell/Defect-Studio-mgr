#include "Core/dspch.hpp"
#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include <glm/glm.hpp>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr const char *kRendererViewportFocusedContext = "renderer.viewport.focused";
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

	// Continuous Ctrl+Shift+Arrow nudge - polled every frame instead of riding GLFW's own key-repeat
	// cadence (see RendererWindowState::continuousNudgeActive), which is OS-repeat-rate limited
	// (~10-15Hz) and visibly steps rather than glides. Called unconditionally (NOT gated on
	// hovered/gizmoCapturing like applyViewportInputNavigation above) - the old keybinding-driven
	// nudge worked as long as the viewport was focused regardless of where the mouse happened to be,
	// and gating this on hover as well was a regression that made it stop firing the moment the mouse
	// drifted off the viewport mid-hold. GetFocusedViewportWindowId() is the same gate the
	// renderer.viewport.focused keybinding context uses, so held arrows don't nudge every open window
	// at once.
	void RendererPanel::applyContinuousNudge(RendererWindowState &windowState, float deltaTime)
	{
		if (windowState.camera == nullptr)
			return;
		ImGuiIO &io = ImGui::GetIO();
		const bool nudgeGateOpen = !windowState.selectedAtomIndices.empty() &&
			m_Layer.GetFocusedViewportWindowId() == windowState.windowId;
		glm::vec2 nudgeScreenDirection(0.0f);
		if (nudgeGateOpen && io.KeyCtrl && io.KeyShift)
		{
			if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
				nudgeScreenDirection.y += 1.0f;
			if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
				nudgeScreenDirection.y -= 1.0f;
			if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
				nudgeScreenDirection.x += 1.0f;
			if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
				nudgeScreenDirection.x -= 1.0f;
		}
		const bool nudgeKeyHeld = nudgeScreenDirection.x != 0.0f || nudgeScreenDirection.y != 0.0f;
		if (windowState.continuousNudgeActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			for (std::size_t i = 0;
				 i < windowState.selectedAtomIndices.size() && i < windowState.continuousNudgeStartPositions.size();
				 ++i)
			{
				windowState.structure.atoms[windowState.selectedAtomIndices[i]].cartesianPosition =
					windowState.continuousNudgeStartPositions[i];
			}
			windowState.continuousNudgeActive = false;
			windowState.continuousNudgeStartPositions.clear();
		}
		else if (nudgeKeyHeld)
		{
			if (!windowState.continuousNudgeActive)
			{
				windowState.continuousNudgeActive = true;
				windowState.continuousNudgeStartPositions.clear();
				for (const std::size_t atomIndex : windowState.selectedAtomIndices)
					windowState.continuousNudgeStartPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
			}
			constexpr float kContinuousNudgeUnitsPerSecond = 2.0f;
			const glm::mat4 view = windowState.camera->ViewMatrix();
			const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
			const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);
			const glm::vec3 worldDelta = (cameraRight * nudgeScreenDirection.x + cameraUp * nudgeScreenDirection.y) *
				(kContinuousNudgeUnitsPerSecond * std::max(0.0f, deltaTime));
			for (const std::size_t atomIndex : windowState.selectedAtomIndices)
			{
				if (atomIndex < windowState.structure.atoms.size())
					windowState.structure.atoms[atomIndex].cartesianPosition += worldDelta;
			}
		}
		else if (windowState.continuousNudgeActive)
		{
			windowState.continuousNudgeActive = false;
			Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
			if (commandRegistry != nullptr)
			{
				GizmoTransformPayload payload;
				payload.windowId = windowState.windowId;
				payload.atomIndices = windowState.selectedAtomIndices;
				payload.afterPositions.reserve(payload.atomIndices.size());
				for (const std::size_t atomIndex : payload.atomIndices)
					payload.afterPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
				payload.description = "Move selected atoms";

				CommandContext context;
				context.Set<GizmoTransformPayload>("gizmo.transform_payload", std::move(payload));
				Result<CommandOutcome> result =
					commandRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Continuous nudge commit failed: {}", result.Error().technicalDetails);
			}
			windowState.continuousNudgeStartPositions.clear();
		}
	}

	// Continuous Alt+Shift+Arrow camera pan - Shift+Arrow alone stays the existing single fixed-step
	// pan (RendererLayer::onPanDirectionRequested); Alt is the same "hold for continuous" modifier
	// orbit already uses (Alt+Arrow), just combined with Shift instead of replacing it, so plain
	// Ctrl+Shift+Arrow keeps meaning atom nudge (applyContinuousNudge above) and this doesn't collide
	// with it. Reuses the same PanDelta event mouse-drag panning publishes, bracketed by a single
	// BeginViewInteraction/CommitViewInteraction pair per hold (see onPanDelta: it only pushes its own
	// view-undo entry when no interaction is already active), matching the mouse-drag pattern instead
	// of riding GLFW's own choppier key-repeat cadence.
	void RendererPanel::applyContinuousPan(RendererWindowState &windowState, float deltaTime)
	{
		if (windowState.camera == nullptr)
			return;
		ImGuiIO &io = ImGui::GetIO();
		const bool panGateOpen = m_Layer.GetFocusedViewportWindowId() == windowState.windowId;
		glm::vec2 pixelDelta(0.0f);
		if (panGateOpen && io.KeyAlt && io.KeyShift)
		{
			// Same up/down sign convention as onPanDirectionRequested - up is negative Y in
			// RendererViewCamera::Pan's own screen-pixel space.
			if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
				pixelDelta.y -= 1.0f;
			if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
				pixelDelta.y += 1.0f;
			if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
				pixelDelta.x += 1.0f;
			if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
				pixelDelta.x -= 1.0f;
		}
		const bool panKeyHeld = pixelDelta.x != 0.0f || pixelDelta.y != 0.0f;
		if (panKeyHeld)
		{
			if (!windowState.continuousPanActive)
			{
				windowState.continuousPanActive = true;
				m_Layer.BeginViewInteraction(windowState.windowId, "keyboard.pan");
			}
			constexpr float kContinuousPanStepsPerSecond = 12.0f;
			const float speed = windowState.pixelStepPx * kContinuousPanStepsPerSecond * std::max(0.0f, deltaTime);
			Ref<EventBus> eventBus = m_Layer.GetEventBus();
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::PanDelta panEvent;
				panEvent.windowId = windowState.windowId;
				panEvent.dx = pixelDelta.x * speed;
				panEvent.dy = pixelDelta.y * speed;
				eventBus->Publish(panEvent);
			}
		}
		else if (windowState.continuousPanActive)
		{
			windowState.continuousPanActive = false;
			m_Layer.CommitViewInteraction(windowState.windowId);
		}
	}

	void RendererPanel::onViewportFocusChanged(const std::string &windowId, bool focused)
	{
		(void)windowId;
		(void)focused;
		auto contextManager = m_ContextManager.lock();
		if (contextManager == nullptr)
			return;

		// Every renderer window shares this one context (there's no per-window keybind scoping),
		// so it must reflect "is *any* viewport focused right now" - not just this window's own
		// transition. Re-deriving it from RendererLayer's already-windowId-aware tracking (set
		// synchronously by the FocusChanged publish just above this call) avoids a last-write-wins
		// race: with two windows changing focus in the same frame (one losing it, one gaining it),
		// whichever window happened to be visited last in the render loop used to decide the
		// result regardless of which one was actually focused - intermittently leaving every
		// renderer.viewport.focused-gated shortcut dead until focus was toggled again.
		contextManager->SetActive(kRendererViewportFocusedContext, !m_Layer.GetFocusedViewportWindowId().empty());
	}
} // namespace DefectStudio
