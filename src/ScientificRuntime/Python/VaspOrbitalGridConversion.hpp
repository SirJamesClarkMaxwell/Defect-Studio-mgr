#pragma once

#include "Domain/Electronic/ElectronicStructureModel.hpp"
#include "ScientificRuntime/Python/VaspOrbitalGridBridge.hpp"

namespace DefectStudio
{
	[[nodiscard]] OrbitalGridData ConvertVaspOrbitalGridDataToDomain(VaspOrbitalGridData gridData);
} // namespace DefectStudio
