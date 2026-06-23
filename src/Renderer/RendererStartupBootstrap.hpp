#pragma once

#include <vector>

#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererStartupDefinitions.hpp"

namespace DefectStudio
{
	[[nodiscard]] std::vector<RendererWindowState> BuildRendererStartupWindows(
		const std::vector<RendererStartupWindowDefinition> &windowDefinitions,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable);
}
