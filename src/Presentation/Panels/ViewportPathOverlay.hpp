#pragma once

#include <imgui.h>

namespace DefectStudio
{
	struct RendererGlobalRenderSettings;
	struct RendererWindowState;

	void DrawViewportPathOverlay(
		const RendererWindowState &windowState, const RendererGlobalRenderSettings &globalSettings,
		const ImVec2 &imageOrigin, const ImVec2 &imageSize);
} // namespace DefectStudio
