#include "Core/dspch.hpp"

#include "Domain/Crystal/CrystalStructure.hpp"

#include <algorithm>
#include <unordered_set>

namespace DefectStudio
{
	std::vector<std::string> CrystalStructure::UniqueSpecies() const
	{
		std::vector<std::string> species;
		species.reserve(atoms.size());

		std::unordered_set<std::string> seen;
		seen.reserve(atoms.size());

		for (const AtomSite &atom : atoms)
		{
			if (atom.species.empty())
				continue;

			auto [iterator, inserted] = seen.insert(atom.species);
			(void)iterator;
			if (inserted)
				species.push_back(atom.species);
		}

		return species;
	}

	bool CrystalStructure::HasAnySelectiveDynamics() const
	{
		return std::any_of(atoms.begin(), atoms.end(), [](const AtomSite &atom) {
			return atom.hasSelectiveDynamics;
		});
	}

	glm::vec3 CrystalStructure::CartesianToFractional(const glm::vec3 &cart) const
	{
		return cell.ToInverseMatrix() * cart;
	}

	glm::vec3 CrystalStructure::FractionalToCartesian(const glm::vec3 &frac) const
	{
		return cell.ToMatrix() * frac;
	}
} // namespace DefectStudio
