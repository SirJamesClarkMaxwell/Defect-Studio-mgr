#include "Core/dspch.hpp"

#include "Domain/Crystal/LatticeBasisExpansion.hpp"

#include <algorithm>
#include <cmath>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] float WrapToUnitInterval(float value)
		{
			const float wrapped = value - std::floor(value);
			// floor() of a tiny negative epsilon returns -1, so wrapped can land exactly on 1.0 and
			// leave the half-open interval this function promises.
			return wrapped >= 1.0f ? 0.0f : wrapped;
		}

		[[nodiscard]] glm::vec3 WrapToUnitCell(const glm::vec3 &fractional)
		{
			return glm::vec3(
				WrapToUnitInterval(fractional.x),
				WrapToUnitInterval(fractional.y),
				WrapToUnitInterval(fractional.z));
		}

		// Distance along one axis with wrap-around: 0.999 and 0.001 are 0.002 apart, not 0.998.
		[[nodiscard]] float PeriodicAxisDistance(float lhs, float rhs)
		{
			const float difference = std::abs(lhs - rhs);
			return std::min(difference, 1.0f - difference);
		}
	} // namespace

	std::vector<AtomSite> ExpandBasisOverLattice(
		std::span<const AtomSite> basis,
		BravaisCenteringPreset centering)
	{
		const std::vector<glm::vec3> translations = GetCenteringTranslations(centering);

		std::vector<AtomSite> expanded;
		expanded.reserve(basis.size() * translations.size());
		int index = 0;
		for (const AtomSite &row : basis)
		{
			for (const glm::vec3 &translation : translations)
			{
				AtomSite atom;
				atom.species = row.species;
				atom.label = row.label;
				atom.charge = row.charge;
				atom.magnetization = row.magnetization;
				atom.occupancy = row.occupancy;
				atom.selectiveDynamics = row.selectiveDynamics;
				atom.hasSelectiveDynamics = row.hasSelectiveDynamics;
				atom.fractional = WrapToUnitCell(row.fractional + translation);
				atom.index = index++;
				expanded.push_back(std::move(atom));
			}
		}
		return expanded;
	}

	std::vector<std::size_t> FindCoincidentAtomIndices(std::span<const AtomSite> atoms, float tolerance)
	{
		// ponytail: O(n^2) pairwise scan. Fine at unit-cell scale (a conventional cell is tens of
		// atoms); switch to a fractional-coordinate hash grid if this ever runs on a supercell.
		std::vector<std::size_t> coincident;
		for (std::size_t i = 0; i < atoms.size(); ++i)
		{
			for (std::size_t j = i + 1; j < atoms.size(); ++j)
			{
				const glm::vec3 first = WrapToUnitCell(atoms[i].fractional);
				const glm::vec3 second = WrapToUnitCell(atoms[j].fractional);
				if (PeriodicAxisDistance(first.x, second.x) > tolerance ||
					PeriodicAxisDistance(first.y, second.y) > tolerance ||
					PeriodicAxisDistance(first.z, second.z) > tolerance)
				{
					continue;
				}
				coincident.push_back(i);
				coincident.push_back(j);
			}
		}
		std::sort(coincident.begin(), coincident.end());
		coincident.erase(std::unique(coincident.begin(), coincident.end()), coincident.end());
		return coincident;
	}
} // namespace DefectStudio
