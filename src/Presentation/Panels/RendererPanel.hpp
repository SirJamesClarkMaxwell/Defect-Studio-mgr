#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include <imgui.h>

#include "Core/Input/KeyInputProcessor.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class ContextManager;
	class EventBus;
	class KeymapResolver;

	class RendererPanel final : public IPanel
	{
	public:
		explicit RendererPanel(
			RendererLayer &layer,
			Ref<EventBus> eventBus,
			WeakRef<KeymapResolver> keymapResolver,
			WeakRef<ContextManager> contextManager,
			std::string title = "Renderer",
			bool visibleByDefault = true);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void render(float deltaTime);
		void renderStructureWindow(RendererWindowState &windowState, float deltaTime);
		void drawViewportToolbar(RendererWindowState &windowState);
		void applyViewportKeyboardNavigation(RendererWindowState &windowState);
		void applyViewportInputNavigation(RendererWindowState &windowState, const ImVec2 &imageOrigin, float deltaTime);
		void onViewportFocusChanged(const std::string &windowId, bool focused);
		void handleAtomPick(RendererWindowState &windowState, float relX, float relY, bool additive);
		void drawPeriodicTableWindow();

	private:
		RendererLayer &m_Layer;
		Ref<EventBus> m_EventBus;
		WeakRef<KeymapResolver> m_KeymapResolver;
		WeakRef<ContextManager> m_ContextManager;
		std::optional<KeyInputProcessor> m_KeyInputProcessor;
		std::unordered_map<std::string, ImVec2> m_LastMousePositions;
	};
} // namespace DefectStudio
