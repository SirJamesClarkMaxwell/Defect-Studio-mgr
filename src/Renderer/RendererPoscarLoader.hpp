#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Renderer/ElementDataTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	[[nodiscard]] Result<RendererStructureData> LoadRendererStructureFromPoscar(
		const Path &filePath,
		std::string name,
		const ElementDataTable &elementTable);
}
