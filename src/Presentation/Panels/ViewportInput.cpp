#include "Core/dspch.hpp"
#include "Presentation/Panels/ViewportInput.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr const char *kRendererViewportFocusedContext = "renderer.viewport.focused";
	} // namespace

	void UpdateViewportFocusState(
		RendererWindowState &windowState,
		bool nowFocused,
		RendererLayer &layer,
		const WeakRef<ContextManager> &contextManager)
	{
		if (nowFocused == windowState.lastFocusedState)
			return;
		windowState.lastFocusedState = nowFocused;

		if (Ref<EventBus> eventBus = layer.GetEventBus(); eventBus != nullptr)
		{
			RendererEvents::Viewport::FocusChanged focusEvent;
			focusEvent.windowId = windowState.windowId;
			focusEvent.focused = nowFocused;
			eventBus->Publish(focusEvent);
		}

		// Every viewport shares this one context (there is no per-window keybind scoping), so it
		// must reflect "is ANY viewport focused right now". Re-derived from the layer rather than
		// from this window state, which is set synchronously by the publish just above: with two
		// windows changing focus in the same frame, whichever was visited last would otherwise
		// decide the result regardless of which one is actually focused.
		if (Ref<ContextManager> resolved = contextManager.lock(); resolved != nullptr)
			resolved->SetActive(kRendererViewportFocusedContext, !layer.GetFocusedViewportWindowId().empty());
	}

	void ApplyViewportInputNavigation(
		RendererWindowState &windowState,
		float deltaTime,
		std::unordered_map<std::string, ImVec2> &lastMousePositions,
		RendererLayer &layer)
	{
		if (windowState.camera == nullptr)
			return;
		Ref<EventBus> eventBus = layer.GetEventBus();
		ImGuiIO &io = ImGui::GetIO();
		const bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
		const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		const bool altPressed = io.KeyAlt;
		const bool shiftPressed = io.KeyShift;
		const bool touchpadOrbit = layer.GetGlobalSettings().touchpadNavigation && altPressed && lmb;
		const bool touchpadPan = layer.GetGlobalSettings().touchpadNavigation && altPressed && shiftPressed && lmb;
		const bool touchpadZoom = layer.GetGlobalSettings().touchpadNavigation && altPressed && rmb;
		const bool dragActiveInput = mmb || touchpadOrbit || touchpadPan || touchpadZoom;
		if (dragActiveInput)
			windowState.transitionActive = false;
		if (!dragActiveInput)
		{
			windowState.dragActive = false;
			lastMousePositions[windowState.windowId] = io.MousePos;
			if (windowState.viewInteractionActive &&
				windowState.viewInteractionSource.rfind("mouse.", 0) == 0)
			{
				layer.CommitViewInteraction(windowState.windowId);
			}
		}
		float wheel = io.MouseWheel;
		if (layer.GetGlobalSettings().invertZoom)
			wheel = -wheel;
		if (wheel != 0.0f)
		{
			layer.BeginViewInteraction(windowState.windowId, "mouse.wheel_zoom");
			windowState.transitionActive = false;
			if (eventBus != nullptr)
			{
				RendererEvents::Viewport::ZoomDelta zoomEvent;
				zoomEvent.windowId = windowState.windowId;
				zoomEvent.amount = wheel * layer.GetGlobalSettings().zoomSensitivity;
				eventBus->Publish(zoomEvent);
			}
			layer.CommitViewInteraction(windowState.windowId);
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
			layer.BeginViewInteraction(windowState.windowId, sourceAction);
			windowState.dragActive = true;
			lastMousePositions[windowState.windowId] = io.MousePos;
			return;
		}
		ImVec2 &lastMousePosition = lastMousePositions[windowState.windowId];
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
				zoomEvent.amount = (-delta.y * 0.020f) * layer.GetGlobalSettings().zoomSensitivity;
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
				panEvent.dx = delta.x * layer.GetGlobalSettings().panSensitivity;
				panEvent.dy = delta.y * layer.GetGlobalSettings().panSensitivity;
				eventBus->Publish(panEvent);
			}
			return;
		}
		if (eventBus != nullptr)
		{
			RendererEvents::Viewport::OrbitDelta orbitEvent;
			orbitEvent.windowId = windowState.windowId;
			orbitEvent.dx = delta.x * layer.GetGlobalSettings().orbitSensitivity;
			orbitEvent.dy = delta.y * layer.GetGlobalSettings().orbitSensitivity;
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
	void ApplyContinuousKeyboardNudge(
		RendererWindowState &windowState,
		float deltaTime,
		RendererLayer &layer,
		const WeakRef<CommandRegistry> &commandRegistry)
	{
		if (windowState.camera == nullptr)
			return;
		ImGuiIO &io = ImGui::GetIO();
		const bool nudgeGateOpen = !windowState.selectedAtomIndices.empty() &&
			layer.GetFocusedViewportWindowId() == windowState.windowId;
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
			Ref<CommandRegistry> resolvedRegistry = commandRegistry.lock();
			if (resolvedRegistry != nullptr)
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
					resolvedRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Continuous nudge commit failed: {}", result.Error().technicalDetails);
			}
			windowState.continuousNudgeStartPositions.clear();
		}
	}

	// Continuous Alt+Shift+Arrow camera pan - Shift+Arrow alone stays the existing single fixed-step
	// pan (RendererLayer::onPanDirectionRequested); Alt is the same "hold for continuous" modifier
	// orbit already uses (Alt+Arrow), just combined with Shift instead of replacing it, so plain
	// Ctrl+Shift+Arrow keeps meaning atom nudge (ApplyContinuousKeyboardNudge above) and this doesn't collide
	// with it. Reuses the same PanDelta event mouse-drag panning publishes, bracketed by a single
	// BeginViewInteraction/CommitViewInteraction pair per hold (see onPanDelta: it only pushes its own
	// view-undo entry when no interaction is already active), matching the mouse-drag pattern instead
	// of riding GLFW's own choppier key-repeat cadence.
	void ApplyContinuousKeyboardPan(RendererWindowState &windowState, float deltaTime, RendererLayer &layer)
	{
		if (windowState.camera == nullptr)
			return;
		ImGuiIO &io = ImGui::GetIO();
		const bool panGateOpen = layer.GetFocusedViewportWindowId() == windowState.windowId;
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
				layer.BeginViewInteraction(windowState.windowId, "keyboard.pan");
			}
			constexpr float kContinuousPanStepsPerSecond = 12.0f;
			const float speed = windowState.pixelStepPx * kContinuousPanStepsPerSecond * std::max(0.0f, deltaTime);
			Ref<EventBus> eventBus = layer.GetEventBus();
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
			layer.CommitViewInteraction(windowState.windowId);
		}
	}
} // namespace DefectStudio
