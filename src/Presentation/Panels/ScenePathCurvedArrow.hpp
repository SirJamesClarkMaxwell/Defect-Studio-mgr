#pragma once

#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	// Two ends make one arrow; three or more atoms make a positive cycle. One undo step.
	[[nodiscard]] Result<std::vector<SceneObjectId>> AddCurvedArrowThroughSelectedAtoms(
		RendererWindowState &windowState);
}
