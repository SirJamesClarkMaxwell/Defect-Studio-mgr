#pragma once

#include <imgui.h>

namespace DefectStudio
{
	struct RendererWindowState;

	void DrawViewportPathOverlay(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize);
} // namespace DefectStudio
