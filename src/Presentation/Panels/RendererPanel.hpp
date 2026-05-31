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
