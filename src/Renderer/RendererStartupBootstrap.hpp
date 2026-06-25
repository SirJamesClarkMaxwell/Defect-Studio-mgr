#pragma once

#include <vector>

#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererStartupDefinitions.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	class PymatgenBridge;

	[[nodiscard]] std::vector<RendererWindowState> BuildRendererStartupWindows(
		const std::vector<RendererStartupWindowDefinition> &windowDefinitions,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable,
		PymatgenBridge *pymatgenBridge = nullptr,
		bool pythonAvailable = false);
}
