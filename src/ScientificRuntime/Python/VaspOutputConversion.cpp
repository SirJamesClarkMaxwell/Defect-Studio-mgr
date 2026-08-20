#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/VaspOutputConversion.hpp"

#include <utility>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] OrbitalChannelData ConvertChannel(const VaspOrbitalChannelData &channel)
		{
			return OrbitalChannelData{channel.energy, channel.occupation, channel.localization, channel.irrep};
		}
	} // namespace

	ElectronicStructureData ConvertVaspOutputDataToElectronicStructureData(const VaspOutputData &outputData)
	{
		ElectronicStructureData data;

		if (outputData.gap.has_value())
			data.gap = BandGapData{outputData.gap->bandgap, outputData.gap->homo, outputData.gap->lumo};

		if (outputData.orbitals.has_value())
		{
			std::vector<OrbitalRecord> records;
			records.reserve(outputData.orbitals->size());
			for (const VaspOrbitalRecord &sourceRecord : *outputData.orbitals)
			{
				OrbitalRecord record;
				record.band = sourceRecord.band;
				record.up = ConvertChannel(sourceRecord.up);
				record.down = ConvertChannel(sourceRecord.down);
				records.push_back(std::move(record));
			}
			data.orbitals = std::move(records);
		}

		data.orbitalsError = outputData.orbitalsError;

		return data;
	}
} // namespace DefectStudio
