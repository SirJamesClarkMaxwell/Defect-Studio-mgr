#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/VaspOrbitalGridConversion.hpp"

namespace DefectStudio
{
	OrbitalGridData ConvertVaspOrbitalGridDataToDomain(VaspOrbitalGridData gridData)
	{
		OrbitalGridData data;
		data.dimensions = gridData.dimensions;
		data.cell = gridData.cell;
		data.values = std::move(gridData.values);
		data.energy = gridData.energy;
		data.occupation = gridData.occupation;
		return data;
	}
} // namespace DefectStudio
