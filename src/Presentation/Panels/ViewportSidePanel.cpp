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
		constexpr float kPanelWidth = 320.0f;
		constexpr float kSlideSeconds = 0.15f;

		struct SlideState
		{
			bool open = false;
			float width = 0.0f;
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

	void DrawViewportSidePanel(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &viewportSize, float deltaTime,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistry, const WeakRef<DomainLayer> &domainLayer)
	{
		SlideState &state = StateFor(windowState.windowId);

		const ImGuiIO &io = ImGui::GetIO();
		const bool plainN = !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && !io.WantTextInput &&
			ImGui::IsKeyPressed(ImGuiKey_N, false);
		if (plainN && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !windowState.modalTransform.has_value())
			state.open = !state.open;

		// Exponential approach rather than a linear ramp - it starts fast and settles, which is what
		// reads as a slide. deltaTime is the renderer's own frame time, so the panel keeps the same
		// timing whatever the frame rate.
		const float target = state.open ? kPanelWidth : 0.0f;
		state.width += (target - state.width) * std::min(1.0f, deltaTime / kSlideSeconds * 3.0f);
		if (std::abs(target - state.width) < 0.5f)
			state.width = target;
		if (state.width < 1.0f)
			return;

		const ImVec2 panelOrigin(imageOrigin.x + viewportSize.x - state.width, imageOrigin.y);
		ImGui::SetCursorScreenPos(panelOrigin);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
		if (ImGui::BeginChild(
				"##viewportSidePanel", ImVec2(state.width, viewportSize.y), ImGuiChildFlags_Borders,
				ImGuiWindowFlags_NoSavedSettings))
		{
			DrawObjectPropertiesContent(layer, commandRegistry, domainLayer);
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}
} // namespace DefectStudio
