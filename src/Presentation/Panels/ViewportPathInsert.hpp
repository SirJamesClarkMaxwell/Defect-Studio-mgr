#pragma once

#include <imgui.h>

namespace DefectStudio
{
	struct RendererWindowState;
	// Runs before gizmos/navigation so the modal owns wheel and click input.
	[[nodiscard]] bool HandleViewportPathInsert(RendererWindowState &window,
		const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
}
