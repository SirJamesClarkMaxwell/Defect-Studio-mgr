#pragma once

#include <string>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	[[nodiscard]] RendererStructureData ConvertCrystalStructureToRendererData(
		const CrystalStructure &structure,
		const Path &sourcePath,
		std::string name,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable);
} // namespace DefectStudio
