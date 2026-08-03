#pragma once

#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	void RegenerateAutoBonds(
		CrystalStructure &structure,
		const ElementPropertiesTable &elementPropertiesTable);
} // namespace DefectStudio
