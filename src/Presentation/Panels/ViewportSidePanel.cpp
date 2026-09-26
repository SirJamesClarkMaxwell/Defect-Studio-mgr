#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSidePanel.hpp"

#include "Presentation/Panels/ObjectPropertiesPanel.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>

#include <imgui.h>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kDefaultPanelWidth = 320.0f;
		constexpr float kMinPanelWidth = 180.0f;
		constexpr float kSlideSeconds = 0.15f;
		constexpr float kGripWidth = 6.0f;
		// The auto-grow-from-overflow path (below) has its own, tighter ceiling than a manual drag:
		// at a high UI font scale, properties content is wider than kDefaultPanelWidth every single
		// frame, so growing it to "viewportSize.x - 40" (the manual-drag ceiling) means it silently
		// eats almost the whole viewport the first time a wide row is drawn. A user dragging the grip
		// still reaches the wider ceiling on purpose; auto-grow should only ever snug the panel to its
		// content, never swallow the view.
		constexpr float kMaxAutoGrowFraction = 0.45f;

		struct SlideState
		{
			bool open = false;
			float width = 0.0f;
			// What the slide animates towards, and what the drag handle edits. Separate from `width`
			// so a resize mid-slide does not fight the animation, and so the width the user chose
			// survives closing and reopening the panel.
			float openWidth = kDefaultPanelWidth;
			// Set the moment the user drags the grip. Until then the panel widens itself to whatever
			// it is asked to draw; afterwards the chosen width is the chosen width and the panel
			// stops second-guessing it.
			bool userSized = false;
		};

		// ponytail: the open flag and the animated width are pure UI state, and RendererWindowState is
		// the task's frozen contract - so they live here, keyed by window id, rather than being
		// threaded through the renderer state. UI is main-thread only, and the map holds two floats
		// per viewport that ever opened the panel; move it onto RendererWindowState if the panel ever
		// needs to survive a project reload.
		SlideState &StateFor(const std::string &windowId)
		{
			static std::unordered_map<std::string, SlideState> states;
			return states[windowId];
		}
	} // namespace

	ViewportSidePanelRect DrawViewportSidePanel(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &viewportSize, float deltaTime,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistry, const WeakRef<DomainLayer> &domainLayer)
	{
		ViewportSidePanelRect panelRect;
		SlideState &state = StateFor(windowState.windowId);

		const ImGuiIO &io = ImGui::GetIO();
		const bool plainN = !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && !io.WantTextInput &&
			ImGui::IsKeyPressed(ImGuiKey_N, false);
		if (plainN && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !windowState.modalTransform.has_value())
			state.open = !state.open;

		// Exponential approach rather than a linear ramp - it starts fast and settles, which is what
		// reads as a slide. deltaTime is the renderer's own frame time, so the panel keeps the same
		// timing whatever the frame rate.
		state.openWidth = std::clamp(state.openWidth, kMinPanelWidth, std::max(kMinPanelWidth, viewportSize.x - 40.0f));
		const float target = state.open ? state.openWidth : 0.0f;
		state.width += (target - state.width) * std::min(1.0f, deltaTime / kSlideSeconds * 3.0f);
		if (std::abs(target - state.width) < 0.5f)
			state.width = target;
		if (state.width < 1.0f)
			return panelRect;

		const ImVec2 panelOrigin(imageOrigin.x + viewportSize.x - state.width, imageOrigin.y);
		panelRect.min = glm::vec2(panelOrigin.x - kGripWidth, panelOrigin.y);
		panelRect.max = glm::vec2(panelOrigin.x + state.width, panelOrigin.y + viewportSize.y);

		// Drag handle on the panel's left edge, drawn before the child so the child does not swallow
		// the click. Width follows the mouse rather than accumulating deltas, so the edge stays under
		// the cursor even if a frame is dropped mid-drag.
		ImGui::SetCursorScreenPos(ImVec2(panelOrigin.x - kGripWidth, panelOrigin.y));
		ImGui::InvisibleButton("##viewportSidePanelGrip", ImVec2(kGripWidth, viewportSize.y));
		const bool gripActive = ImGui::IsItemActive();
		if (ImGui::IsItemHovered() || gripActive)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		if (gripActive)
		{
			state.userSized = true;
			state.openWidth = std::clamp(
				imageOrigin.x + viewportSize.x - ImGui::GetMousePos().x, kMinPanelWidth,
				std::max(kMinPanelWidth, viewportSize.x - 40.0f));
		}
		if (ImGui::IsItemHovered() || gripActive)
		{
			ImGui::GetWindowDrawList()->AddRectFilled(
				ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
				ImGui::GetColorU32(gripActive ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered));
		}

		ImGui::SetCursorScreenPos(panelOrigin);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
		if (ImGui::BeginChild(
				"##viewportSidePanel", ImVec2(state.width, viewportSize.y), ImGuiChildFlags_Borders,
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_HorizontalScrollbar))
		{
			DrawObjectPropertiesContent(layer, commandRegistry, domainLayer);
			// Widen by exactly what overflowed, measured after the content was submitted. It settles
			// in a frame or two and the scrollbar it needs to measure with disappears once it has.
			// ponytail: this only sees content that actually extends the child's width - a
			// TextWrapped paragraph re-wraps instead of overflowing, so it never asks for room. If
			// the descriptions start looking cramped, give them a measured minimum instead.
			const float overflow = ImGui::GetScrollMaxX();
			if (!state.userSized && overflow > 1.0f)
			{
				const float autoGrowCeiling = std::max(kMinPanelWidth, viewportSize.x * kMaxAutoGrowFraction);
				state.openWidth = std::min(state.openWidth + overflow, autoGrowCeiling);
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		return panelRect;
	}
} // namespace DefectStudio
