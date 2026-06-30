#pragma once

#include <vector>

#include "Renderer/RendererStartupDefinitions.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	struct RendererStartupWindowInput
	{
		RendererStartupWindowDefinition definition;
		RendererStructureData structure;
	};

	[[nodiscard]] std::vector<RendererWindowState> BuildRendererStartupWindows(
		std::vector<RendererStartupWindowInput> windowInputs);
}
