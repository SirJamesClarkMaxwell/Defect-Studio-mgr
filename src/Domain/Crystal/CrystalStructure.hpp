#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Domain/Crystal/AtomSite.hpp"
#include "Domain/Crystal/LatticeCell.hpp"
#include "Domain/Crystal/VacancySite.hpp"

namespace DefectStudio
{
	struct CrystalStructure
	{
		std::string name;
		LatticeCell cell;
		std::vector<AtomSite> atoms;
		std::vector<VacancySite> vacancies;
		bool isPeriodic = true;

		[[nodiscard]] std::vector<std::string> UniqueSpecies() const;
		[[nodiscard]] glm::vec3 CartesianToFractional(const glm::vec3 &cart) const;
		[[nodiscard]] glm::vec3 FractionalToCartesian(const glm::vec3 &frac) const;
	};
} // namespace DefectStudio
