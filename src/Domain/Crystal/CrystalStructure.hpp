#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Domain/Crystal/CrystalPrimitives.hpp"

namespace DefectStudio
{
	struct CrystalStructure
	{
		std::string name;
		LatticeCell cell;
		std::vector<AtomSite> atoms;
		std::vector<VacancySite> vacancies;
		// Saved in the scene_objects.yaml sidecar like `vacancies` - a POSCAR has no place for it.
		std::optional<DefectFrame> defectFrame;
		std::vector<Bond> bonds;
		BondGenerationSettings bondSettings;
		bool isPeriodic = true;

		[[nodiscard]] std::vector<std::string> UniqueSpecies() const;
		[[nodiscard]] bool HasAnySelectiveDynamics() const;
		[[nodiscard]] glm::vec3 CartesianToFractional(const glm::vec3 &cart) const;
		[[nodiscard]] glm::vec3 FractionalToCartesian(const glm::vec3 &frac) const;
	};
} // namespace DefectStudio
