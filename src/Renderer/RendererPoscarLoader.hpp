#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	[[nodiscard]] Result<RendererStructureData> LoadRendererStructureFromPoscar(
		const Path &filePath,
		std::string name,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable);
}
