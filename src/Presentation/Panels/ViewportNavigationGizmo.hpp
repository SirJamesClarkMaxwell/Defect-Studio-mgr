#pragma once

#include <imgui.h>

namespace DefectStudio
{
	class RendererLayer;
	struct RendererWindowState;

	// Draws the viewport-corner navigation control and returns whether it owns mouse input.
	[[nodiscard]] bool RenderViewportNavigationGizmo(
		RendererWindowState &windowState,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize,
		bool viewportHovered,
		float horizontalToolbarOffset,
		RendererLayer &layer);
} // namespace DefectStudio
