#pragma once

#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class RendererPanel
	{
	public:
		explicit RendererPanel(RendererLayer &layer);

		void Render(float deltaTime);

	private:
		void renderStructureWindow(RendererWindowState &windowState, float deltaTime);
		void drawViewportToolbar(RendererWindowState &windowState);
		void applyViewportKeyboardNavigation(RendererWindowState &windowState);
		void applyViewportInputNavigation(RendererWindowState &windowState, const ImVec2 &imageOrigin, float deltaTime);
		void handleAtomPick(RendererWindowState &windowState, float relX, float relY, bool additive);
		void drawPeriodicTableWindow();
		[[nodiscard]] RendererViewSnapshot captureViewSnapshot(const RendererWindowState &windowState) const;
		void restoreViewSnapshot(RendererWindowState &windowState, const RendererViewSnapshot &snapshot, const char *sourceAction);
		void beginViewInteraction(RendererWindowState &windowState, const char *sourceAction);
		void commitViewInteraction(RendererWindowState &windowState);
		void cancelViewInteraction(RendererWindowState &windowState);
		void pushViewChange(
			RendererWindowState &windowState,
			const RendererViewSnapshot &before,
			const RendererViewSnapshot &after,
			const char *sourceAction);
		void undoViewChange(RendererWindowState &windowState);
		void redoViewChange(RendererWindowState &windowState);
		void startCameraTransition(
			RendererWindowState &windowState,
			const glm::vec3 &target,
			float distance,
			float yaw,
			float pitch,
			float roll,
			const char *sourceAction = nullptr);
		void updateCameraTransition(RendererWindowState &windowState, float deltaTime);

	private:
		RendererLayer &m_Layer;
	};
} // namespace DefectStudio
