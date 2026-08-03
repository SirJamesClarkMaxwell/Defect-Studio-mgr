#pragma once

#include <string>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	[[nodiscard]] RendererStructureData BuildRendererStructureData(
		const CrystalStructure &structure,
		const Path &sourcePath,
		std::string name,
		const AtomStyleTable &atomStyleTable,
		std::string domainStructureId = {});
} // namespace DefectStudio
