#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class ContextManager;
	class EventBus;

	class RendererPanel final : public IPanel
	{
	public:
		explicit RendererPanel(
			RendererLayer &layer,
			Ref<EventBus> eventBus,
			WeakRef<ContextManager> contextManager,
			WeakRef<CommandRegistry> commandRegistry = {},
			std::string title = "Renderer",
			bool visibleByDefault = true);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void render(float deltaTime);
		void renderStructureWindow(
			RendererWindowState &windowState, float deltaTime, std::vector<std::string> &windowsToClose);
		void drawViewportToolbar(RendererWindowState &windowState);
		void drawViewportVerticalToolbar(RendererWindowState &windowState);
		void applyViewportInputNavigation(RendererWindowState &windowState, const ImVec2 &imageOrigin, float deltaTime);
		void applyContinuousNudge(RendererWindowState &windowState, float deltaTime);
		void applyContinuousPan(RendererWindowState &windowState, float deltaTime);
		void onViewportFocusChanged(const std::string &windowId, bool focused);
		void handleAtomPick(RendererWindowState &windowState, float relX, float relY, bool additive);
		void handleBoxSelectDrag(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered);
		void handleCircleSelectDrag(RendererWindowState &windowState, const ImVec2 &imageOrigin, bool hovered);
		[[nodiscard]] std::vector<std::size_t> hitTestRect(
			const RendererWindowState &windowState, glm::vec2 rectMin, glm::vec2 rectMax) const;
		[[nodiscard]] std::vector<std::size_t> hitTestCircle(
			const RendererWindowState &windowState, glm::vec2 center, float radius) const;
		[[nodiscard]] static RendererEvents::Viewport::RegionSelectMode resolveRegionSelectMode(bool additive, bool subtractive);
		void publishRegionSelection(
			RendererWindowState &windowState,
			std::vector<std::size_t> atomIndices,
			RendererEvents::Viewport::RegionSelectMode mode);
		void drawPeriodicTableWindow();
		[[nodiscard]] bool renderTransformGizmo(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		[[nodiscard]] bool renderLabelTransformGizmo(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		[[nodiscard]] bool handlePinnedMeasurementInteraction(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
		[[nodiscard]] bool handleCursor3DPlacement(
			RendererWindowState &windowState, float relX, float relY);
		[[nodiscard]] glm::vec3 computeViewportWorldPosition(
			const RendererWindowState &windowState, float relX, float relY) const;
		[[nodiscard]] bool isAtomUnderScreenPosition(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPos) const;
		void renderViewportContextMenu(
			RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);

	private:
		RendererLayer &m_Layer;
		Ref<EventBus> m_EventBus;
		WeakRef<ContextManager> m_ContextManager;
		WeakRef<CommandRegistry> m_CommandRegistry;
		std::unordered_map<std::string, ImVec2> m_LastMousePositions;
		// Snapshot of the right-click's world position, taken the frame the viewport context menu
		// opens (ImGui::IsWindowAppearing()) - "Set 3D cursor here" reads it later, when the user
		// actually clicks that menu item and the live mouse position no longer points at the click.
		// Only one context menu can be open at a time app-wide, so a single field is enough.
		glm::vec3 m_ContextMenuWorldPosition = glm::vec3(0.0f);
	};
} // namespace DefectStudio
