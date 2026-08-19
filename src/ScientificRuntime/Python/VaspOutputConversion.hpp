#pragma once

#include "Domain/Electronic/ElectronicStructureModel.hpp"
#include "ScientificRuntime/Python/VaspOutputBridge.hpp"

namespace DefectStudio
{
	[[nodiscard]] ElectronicStructureData ConvertVaspOutputDataToElectronicStructureData(
		const VaspOutputData &outputData);
} // namespace DefectStudio
