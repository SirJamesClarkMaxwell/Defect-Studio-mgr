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

	namespace
	{
		[[nodiscard]] std::size_t GridSampleIndex(const glm::ivec3 &dims, int i, int j, int k)
		{
			i = std::clamp(i, 0, dims.x - 1);
			j = std::clamp(j, 0, dims.y - 1);
			k = std::clamp(k, 0, dims.z - 1);
			return (static_cast<std::size_t>(i) * dims.y + j) * dims.z + k;
		}
	} // namespace

	OrbitalGridData UpsampleOrbitalGrid(const OrbitalGridData &grid, int factor)
	{
		if (factor <= 1 || grid.dimensions.x < 2 || grid.dimensions.y < 2 || grid.dimensions.z < 2)
			return grid;

		const glm::ivec3 oldDims = grid.dimensions;
		const glm::ivec3 newDims(
			(oldDims.x - 1) * factor + 1, (oldDims.y - 1) * factor + 1, (oldDims.z - 1) * factor + 1);

		OrbitalGridData result;
		result.dimensions = newDims;
		result.cell = grid.cell;
		result.energy = grid.energy;
		result.occupation = grid.occupation;
		result.values.resize(static_cast<std::size_t>(newDims.x) * newDims.y * newDims.z);

		for (int i = 0; i < newDims.x; ++i)
		{
			const float fi = static_cast<float>(i) / static_cast<float>(factor);
			const int i0 = static_cast<int>(std::floor(fi));
			const float ti = fi - static_cast<float>(i0);
			for (int j = 0; j < newDims.y; ++j)
			{
				const float fj = static_cast<float>(j) / static_cast<float>(factor);
				const int j0 = static_cast<int>(std::floor(fj));
				const float tj = fj - static_cast<float>(j0);
				for (int k = 0; k < newDims.z; ++k)
				{
					const float fk = static_cast<float>(k) / static_cast<float>(factor);
					const int k0 = static_cast<int>(std::floor(fk));
					const float tk = fk - static_cast<float>(k0);

					const float c000 = grid.values[GridSampleIndex(oldDims, i0, j0, k0)];
					const float c100 = grid.values[GridSampleIndex(oldDims, i0 + 1, j0, k0)];
					const float c010 = grid.values[GridSampleIndex(oldDims, i0, j0 + 1, k0)];
					const float c110 = grid.values[GridSampleIndex(oldDims, i0 + 1, j0 + 1, k0)];
					const float c001 = grid.values[GridSampleIndex(oldDims, i0, j0, k0 + 1)];
					const float c101 = grid.values[GridSampleIndex(oldDims, i0 + 1, j0, k0 + 1)];
					const float c011 = grid.values[GridSampleIndex(oldDims, i0, j0 + 1, k0 + 1)];
					const float c111 = grid.values[GridSampleIndex(oldDims, i0 + 1, j0 + 1, k0 + 1)];

					const float c00 = std::lerp(c000, c100, ti);
					const float c10 = std::lerp(c010, c110, ti);
					const float c01 = std::lerp(c001, c101, ti);
					const float c11 = std::lerp(c011, c111, ti);
					const float c0 = std::lerp(c00, c10, tj);
					const float c1 = std::lerp(c01, c11, tj);
					const std::size_t destIndex = (static_cast<std::size_t>(i) * newDims.y + j) * newDims.z + k;
					result.values[destIndex] = std::lerp(c0, c1, tk);
				}
			}
		}
		return result;
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
		const bool upEmpty = upOccupation < kOccupationEpsilon;
		const bool downEmpty = downOccupation < kOccupationEpsilon;
		if (upEmpty && downEmpty)
			return SpinMultiplicity::Unknown;
		if (upEmpty || downEmpty)
			return SpinMultiplicity::Singlet;

		return std::abs(upOccupation - downOccupation) < kOccupationEpsilon
			? SpinMultiplicity::Singlet
			: SpinMultiplicity::Triplet;
	}
} // namespace DefectStudio
