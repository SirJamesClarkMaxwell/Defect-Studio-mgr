#pragma once

#include <vector>

#include "Core/Utils/Path.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	[[nodiscard]] std::vector<RendererWindowState> BuildRendererStartupWindows(const Path &assetsDirectory);
}
