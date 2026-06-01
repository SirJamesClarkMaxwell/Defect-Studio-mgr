#pragma once

#include <array>
#include <string>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct AtomSite
	{
		std::string species;
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 fractional = glm::vec3(0.0f);
		int index = 0;
	};

	struct VacancySite
	{
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 fractional = glm::vec3(0.0f);
		std::string sourceSpecies;
		std::string label;
		int index = 0;

		[[nodiscard]] std::string GetLabel() const
		{
			if (!label.empty())
				return label;
			if (sourceSpecies.empty())
				return "V";
			return "V_" + sourceSpecies;
		}
	};

	struct LatticeCell
	{
		std::array<glm::vec3, 3> vectors = {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f)};

		[[nodiscard]] glm::mat3 ToMatrix() const;
		[[nodiscard]] glm::mat3 ToInverseMatrix() const;
	};
} // namespace DefectStudio
