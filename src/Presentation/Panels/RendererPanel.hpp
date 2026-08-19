#pragma once

#include <string>
#include <unordered_map>

#include <imgui.h>

#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class ContextManager;
	class EventBus;

	class RendererPanel final : public IPanel
	{
	public:
		explicit RendererPanel(
			RendererLayer &layer,
			Ref<EventBus> eventBus,
			WeakRef<ContextManager> contextManager,
			std::string title = "Renderer",
			bool visibleByDefault = true);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void render(float deltaTime);
		void renderStructureWindow(RendererWindowState &windowState, float deltaTime);
		void drawViewportToolbar(RendererWindowState &windowState);
		void applyViewportInputNavigation(RendererWindowState &windowState, const ImVec2 &imageOrigin, float deltaTime);
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

	private:
		RendererLayer &m_Layer;
		Ref<EventBus> m_EventBus;
		WeakRef<ContextManager> m_ContextManager;
		std::unordered_map<std::string, ImVec2> m_LastMousePositions;
	};
} // namespace DefectStudio
