#include "Core/dspch.hpp"

#include "Domain/Electronic/ElectronicStructureModel.hpp"

#include <algorithm>
#include <cmath>

namespace DefectStudio
{
	std::vector<OrbitalRecord> FilterByLocalizationThreshold(
		const std::vector<OrbitalRecord> &orbitals,
		const LocalizationThresholdSettings &settings)
	{
		std::vector<OrbitalRecord> filtered;
		filtered.reserve(orbitals.size());
		for (const OrbitalRecord &record : orbitals)
		{
			const float maxLocalization = std::max(record.up.localization, record.down.localization);
			if (maxLocalization >= settings.minLocalization)
				filtered.push_back(record);
		}
		return filtered;
	}

	SpinMultiplicity ClassifySpinMultiplicity(const std::vector<OrbitalRecord> &orbitalsInWindow)
	{
		if (orbitalsInWindow.empty())
			return SpinMultiplicity::Unknown;

		float upOccupation = 0.0f;
		float downOccupation = 0.0f;
		for (const OrbitalRecord &record : orbitalsInWindow)
		{
			upOccupation += record.up.occupation;
			downOccupation += record.down.occupation;
		}

		constexpr float kOccupationEpsilon = 1e-3f;
		return std::abs(upOccupation - downOccupation) < kOccupationEpsilon
			? SpinMultiplicity::Singlet
			: SpinMultiplicity::Triplet;
	}
} // namespace DefectStudio
