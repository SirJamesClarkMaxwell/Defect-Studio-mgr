#pragma once

#include <string>

#include "Domain/Crystal/CrystalStructure.hpp"
#include "ScientificRuntime/Python/PymatgenBridge.hpp"

namespace DefectStudio
{
	[[nodiscard]] CrystalStructure ConvertPymatgenStructureToCrystalStructure(
		const PymatgenStructureData &structureData,
		std::string name = {});
} // namespace DefectStudio
